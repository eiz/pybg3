// pybg3
//
// Copyright (C) 2024 Mackenzie Straight.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <memory>
#include <unordered_map>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define LIBBG3_IMPLEMENTATION
#include "libbg3.h"

#include "pybg3_granny.h"
#include "rans.h"

namespace py = pybind11;

int osiris_decompile_path(const std::string& src, const std::string& dst) {
  bg3_status status = bg3_success;
  bg3_mapped_file file;
  if ((status = bg3_mapped_file_init_ro(&file, src.c_str()))) {
    return status;
  }
  bg3_osiris_save save;
  bg3_osiris_save_init_binary(&save, file.data, file.data_len);
  status = bg3_osiris_save_write_sexp(&save, dst.c_str(), false);
  bg3_osiris_save_destroy(&save);
  bg3_mapped_file_destroy(&file);
  return status;
}

int osiris_compile_path(const std::string& src, const std::string& dst) {
  bg3_status status = bg3_success;
  bg3_mapped_file file;
  if ((status = bg3_mapped_file_init_ro(&file, src.c_str()))) {
    return status;
  }
  bg3_osiris_save_builder builder;
  bg3_osiris_save_builder_init(&builder);
  status = bg3_osiris_save_builder_parse(&builder, file.data, file.data_len);
  if (!status) {
    status = bg3_osiris_save_builder_finish(&builder);
  }
  if (!status) {
    status = bg3_osiris_save_write_binary(&builder.save, dst.c_str());
  }
  bg3_osiris_save_builder_destroy(&builder);
  bg3_mapped_file_destroy(&file);
  return status;
}

struct py_lspk_file {
  py_lspk_file(const std::string& path) {
    bg3_status status = bg3_mapped_file_init_ro(&mapped, path.c_str());
    if (status) {
      throw std::runtime_error("Failed to open lspk file");
    }
    status = bg3_lspk_file_init(&lspk, &mapped);
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse lspk file");
    }
  }
  ~py_lspk_file() {
    bg3_lspk_file_destroy(&lspk);
    bg3_mapped_file_destroy(&mapped);
  }
  size_t num_files() { return lspk.num_files; }
  std::string file_name(size_t idx) {
    if (idx >= lspk.num_files) {
      throw std::runtime_error("Index out of bounds");
    }
    return std::string(lspk.manifest[idx].name);
  }
  size_t file_size(size_t idx) {
    if (idx >= lspk.num_files) {
      throw std::runtime_error("Index out of bounds");
    }
    bg3_lspk_manifest_entry* entry = &lspk.manifest[idx];
    if (LIBBG3_LSPK_ENTRY_COMPRESSION_METHOD(entry->compression) ==
        LIBBG3_LSPK_ENTRY_COMPRESSION_NONE) {
      return entry->compressed_size;
    }
    return entry->uncompressed_size;
  }
  int priority() { return lspk.header.priority; }
  py::bytes file_data(size_t idx) {
    if (idx >= lspk.num_files) {
      throw std::runtime_error("Index out of bounds");
    }
    size_t size = file_size(idx);
    std::string buf(size, 0);
    bg3_status status =
        bg3_lspk_file_extract(&lspk, &lspk.manifest[idx], buf.data(), &size);
    if (status) {
      throw std::runtime_error("Failed to extract file");
    }
    return py::bytes(buf);
  }
  bg3_mapped_file mapped;
  bg3_lspk_file lspk;
};

static py::object convert_value(bg3_lsof_dt type, char* value_bytes, size_t length) {
  // There's an unfortunate amount of pasta from bg3_lsof_reader_print_sexp
  // here. TODO: create some kind of variant struct that these can be expanded
  // out into so this kind of code isn't so hairy
  switch (type) {
    case bg3_lsof_dt_lsstring:
    case bg3_lsof_dt_fixedstring:
      return py::str(value_bytes, length - 1);
    case bg3_lsof_dt_bool:
      return py::bool_(*value_bytes != 0);
    case bg3_lsof_dt_uuid: {
      bg3_uuid id;
      if (length != sizeof(bg3_uuid)) {
        bg3_panic("invalid uuid length");
      }
      memcpy(&id, value_bytes, sizeof(id));
      bg3_buffer tmp = {};
      bg3_buffer_printf(&tmp, "%08x-%04x-%04x-%04x-%04x%04x%04x\")", id.word, id.half[0],
                        id.half[1], id.half[2], id.half[3], id.half[4], id.half[5]);
      py::object result = py::str(tmp.data, tmp.size);
      bg3_buffer_destroy(&tmp);
      return result;
    }
    case bg3_lsof_dt_translatedstring: {
      uint16_t version;
      uint32_t string_len;
      memcpy(&version, value_bytes, sizeof(uint16_t));
      memcpy(&string_len, value_bytes + 2, sizeof(uint32_t));
      if (string_len != length - 6) {
        bg3_panic("invalid translated string length");
      }
      return py::make_tuple(py::str(value_bytes + 6, string_len), version);
    }
    case bg3_lsof_dt_ivec2: {
      struct {
        int32_t x, y;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y);
    }
    case bg3_lsof_dt_ivec3: {
      struct {
        int32_t x, y, z;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y, vec.z);
    }
    case bg3_lsof_dt_ivec4: {
      struct {
        int32_t x, y, z, w;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y, vec.z, vec.w);
    }
    case bg3_lsof_dt_vec2: {
      struct {
        float x, y;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y);
    }
    case bg3_lsof_dt_vec3: {
      struct {
        float x, y, z;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y, vec.z);
    }
    case bg3_lsof_dt_vec4: {
      struct {
        float x, y, z, w;
      } vec;
      memcpy(&vec, value_bytes, sizeof(vec));
      return py::make_tuple(vec.x, vec.y, vec.z, vec.w);
    }
#define V(dt, itype)                          \
  case dt: {                                  \
    itype val;                                \
    memcpy(&val, value_bytes, sizeof(itype)); \
    return py::int_(val);                     \
  }
      V(bg3_lsof_dt_uint8, uint8_t);
      V(bg3_lsof_dt_int8, int8_t);
      V(bg3_lsof_dt_uint16, uint16_t);
      V(bg3_lsof_dt_int16, int16_t);
      V(bg3_lsof_dt_uint32, uint32_t);
      V(bg3_lsof_dt_int32, int32_t);
      V(bg3_lsof_dt_uint64, int64_t);
      V(bg3_lsof_dt_int64, int64_t);
#undef V
#define V(dt, itype)                          \
  case dt: {                                  \
    itype val;                                \
    memcpy(&val, value_bytes, sizeof(itype)); \
    return py::float_(val);                   \
  }
      V(bg3_lsof_dt_float, float);
      V(bg3_lsof_dt_double, double);
#undef V
    default:
      return py::bytes(value_bytes, length);
  }
}

struct py_lsof_file {
  static std::unique_ptr<py_lsof_file> from_path(py::str path) {
    return std::make_unique<py_lsof_file>(path);
  }
  static std::unique_ptr<py_lsof_file> from_data(py::bytes data) {
    return std::make_unique<py_lsof_file>(data);
  }
  py_lsof_file(py::str py_path) {
    std::string path(py_path);
    bg3_status status = bg3_mapped_file_init_ro(&mapped, path.c_str());
    if (status) {
      throw std::runtime_error("Failed to open lsof file");
    }
    status = bg3_lsof_reader_init(&reader, mapped.data, mapped.data_len);
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse lsof file");
    }
    is_mapped_file = true;
  }
  py_lsof_file(py::bytes data) : data(data) {
    std::string_view view(data);
    // TODO: lsof reader isnt const correct
    bg3_status status =
        bg3_lsof_reader_init(&reader, const_cast<char*>(view.data()), view.size());
    if (status) {
      throw std::runtime_error("Failed to parse lsof file");
    }
  }
  ~py_lsof_file() {
    if (is_mapped_file) {
      bg3_mapped_file_destroy(&mapped);
    }
    bg3_lsof_reader_destroy(&reader);
  }
  std::string to_sexp() {
    bg3_buffer tmp_buf = {};
    bg3_lsof_reader_print_sexp(&reader, &tmp_buf);
    std::string result(tmp_buf.data, tmp_buf.size);
    bg3_buffer_destroy(&tmp_buf);
    return result;  // 3 string allocations, lol
  }
  bool is_wide() {
    return LIBBG3_IS_SET(reader.header.flags, LIBBG3_LSOF_FLAG_HAS_SIBLING_POINTERS);
  }
  size_t num_nodes() { return reader.num_nodes; }
  size_t num_attrs() { return reader.num_attrs; }
  py::tuple node(size_t idx) {
    bg3_lsof_node_wide n;
    if (bg3_lsof_reader_get_node(&reader, &n, idx)) {
      throw std::runtime_error("Index out of bounds");
    }
    bg3_lsof_symtab_entry* sym = bg3_lsof_symtab_get_ref(&reader.symtab, n.name);
    std::string name(sym->data, sym->length);
    return py::make_tuple(name, n.parent, n.next, n.attrs);
  }
  py::tuple attr(size_t idx) {
    bg3_lsof_attr_wide a;
    if (bg3_lsof_reader_get_attr(&reader, &a, idx)) {
      throw std::runtime_error("Index out of bounds");
    }
    // TODO: this whole API situation is really bad on the C level. think we need an
    // attribute iterator api or something
    bg3_lsof_symtab_entry* sym = bg3_lsof_symtab_get_ref(&reader.symtab, a.name);
    std::string name(sym->data, sym->length);
    bool wide = is_wide();
    bg3_lsof_reader_ensure_value_offsets(&reader);
    size_t offset = wide ? a.value : reader.value_offsets[idx];
    py::object owner = py::none();
    if (!wide) {
      owner = py::int_(a.owner);
    }
    char* value_bytes = reader.value_table_raw + offset;
    return py::make_tuple(name, (int)a.type, a.next, owner,
                          convert_value((bg3_lsof_dt)a.type, value_bytes, a.length));
  }
  void ensure_sibling_pointers() { bg3_lsof_reader_ensure_sibling_pointers(&reader); }
  void scan_unique_objects(py::function callback,
                           std::string name_key,
                           std::string uuid_key,
                           int32_t node_idx) {
    ensure_sibling_pointers();
    bg3_lsof_node_wide* nodes = (bg3_lsof_node_wide*)reader.node_table_raw;
    bg3_lsof_attr_wide* attrs = (bg3_lsof_attr_wide*)reader.attr_table_raw;
    bg3_lsof_sym_ref name_ref{};
    bg3_lsof_sym_ref uuid_ref{};
    bool has_name_ref{};
    bool has_uuid_ref{};
    for (; node_idx < reader.num_nodes && node_idx != -1;
         node_idx = nodes[node_idx].next) {
      int32_t found_name = -1, found_uuid = -1;
      for (int32_t attr_idx = nodes[node_idx].attrs;
           (found_name == -1 || found_uuid == -1) && attr_idx != -1;
           attr_idx = attrs[attr_idx].next) {
        bg3_lsof_attr_wide* a = attrs + attr_idx;
        if (a->type != bg3_lsof_dt_fixedstring && a->type != bg3_lsof_dt_lsstring) {
          continue;
        }
        if (found_name == -1) {
          if (has_name_ref) {
            found_name =
                a->name.bucket == name_ref.bucket && a->name.entry == name_ref.entry
                    ? attr_idx
                    : -1;
          } else {
            bg3_lsof_symtab_entry* sym = bg3_lsof_symtab_get_ref(&reader.symtab, a->name);
            if (sym->length == name_key.size() &&
                !memcmp(sym->data, name_key.data(), name_key.size())) {
              found_name = attr_idx;
              has_name_ref = true;
              memcpy(&name_ref, &a->name, sizeof(bg3_lsof_sym_ref));
            }
          }
        }
        if (found_uuid == -1) {
          if (has_uuid_ref) {
            found_uuid =
                a->name.bucket == uuid_ref.bucket && a->name.entry == uuid_ref.entry
                    ? attr_idx
                    : -1;
          } else {
            bg3_lsof_symtab_entry* sym = bg3_lsof_symtab_get_ref(&reader.symtab, a->name);
            if (sym->length == uuid_key.size() &&
                !memcmp(sym->data, uuid_key.data(), uuid_key.size())) {
              found_uuid = attr_idx;
              has_uuid_ref = true;
              memcpy(&uuid_ref, &a->name, sizeof(bg3_lsof_sym_ref));
            }
          }
        }
      }
      if (found_name != -1 && found_uuid != -1) {
        py::str py_name(reader.value_table_raw + attrs[found_name].value);
        py::str py_uuid(reader.value_table_raw + attrs[found_uuid].value);
        callback(py::int_(node_idx), py_name, py_uuid);
      }
    }
  }
  py::tuple stats() {
    return py::make_tuple(reader.header.string_table.uncompressed_size,
                          reader.header.node_table.uncompressed_size,
                          reader.header.attr_table.uncompressed_size,
                          reader.header.value_table.uncompressed_size);
  }
  bool is_mapped_file{false};
  py::bytes data;
  bg3_mapped_file mapped;
  bg3_lsof_reader reader;
};

struct py_loca_file {
  static std::unique_ptr<py_loca_file> from_path(py::str path) {
    return std::make_unique<py_loca_file>(path);
  }
  static std::unique_ptr<py_loca_file> from_data(py::bytes data) {
    return std::make_unique<py_loca_file>(data);
  }
  py_loca_file(py::str py_path) {
    std::string path(py_path);
    bg3_status status = bg3_mapped_file_init_ro(&mapped, path.c_str());
    if (status) {
      throw std::runtime_error("Failed to open loca file");
    }
    status = bg3_loca_reader_init(&reader, mapped.data, mapped.data_len);
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse loca file");
    }
    is_mapped_file = true;
  }
  py_loca_file(py::bytes data) : data(data) {
    std::string_view view(data);
    bg3_status status = bg3_loca_reader_init(&reader, (char*)view.data(), view.size());
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse loca file");
    }
  }
  ~py_loca_file() {
    if (is_mapped_file) {
      bg3_mapped_file_destroy(&mapped);
    }
    bg3_loca_reader_destroy(&reader);
  }
  size_t num_entries() { return reader.header.num_entries; }
  py::tuple entry(size_t idx) {
    if (idx >= reader.header.num_entries) {
      throw std::runtime_error("Index out of bounds");
    }
    bg3_loca_reader_entry* entry = &reader.entries[idx];
    return py::make_tuple(entry->handle, entry->version,
                          std::string(entry->data, entry->data_size - 1));
  }
  bool is_mapped_file{false};
  bg3_mapped_file mapped;
  py::bytes data;
  bg3_loca_reader reader;
};

struct py_index_reader {
  py_index_reader(const std::string& path) {
    bg3_status status = bg3_mapped_file_init_ro(&mapped, path.c_str());
    if (status) {
      throw std::runtime_error("Failed to open index file");
    }
    status = bg3_index_reader_init(&reader, mapped.data, mapped.data_len);
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse index file");
    }
  }
  ~py_index_reader() {
    bg3_index_reader_destroy(&reader);
    bg3_mapped_file_destroy(&mapped);
  }
  std::vector<py::tuple> query(const std::string& query_str) {
    std::vector<py::tuple> output;
    bg3_index_search_results results;
    bg3_index_reader_query(&reader, &results, query_str.c_str());
    for (size_t i = 0; i < results.num_hits; ++i) {
      bg3_index_search_hit* hit = results.hits + i;
      auto& pak_intern = intern[hit->pak->name];
      if (!pak_intern) {
        pak_intern = py::str(hit->pak->name);
      }
      auto& file_intern = intern[hit->file->name];
      if (!file_intern) {
        file_intern = py::str(hit->file->name);
      }
      output.emplace_back(py::make_tuple(pak_intern, file_intern, hit->value));
    }
    bg3_index_search_results_destroy(&results);
    return output;
  }
  std::unordered_map<std::string, py::object> intern;
  bg3_mapped_file mapped;
  bg3_index_reader reader;
};

struct py_granny_reader;

static size_t granny_data_type_size(bg3_granny_data_type dt) {
  switch (dt) {
    case bg3_granny_dt_inline:
      return 0;
    case bg3_granny_dt_reference:
      return sizeof(void*);
    case bg3_granny_dt_reference_to_array:
      return sizeof(void*) + sizeof(int32_t);
    case bg3_granny_dt_array_of_references:
      return sizeof(void*) + sizeof(int32_t);
    case bg3_granny_dt_variant_reference:
      return sizeof(bg3_granny_variant);
    case bg3_granny_dt_reference_to_variant_array:
      return sizeof(bg3_granny_variant_array);
    case bg3_granny_dt_string:
      return sizeof(char*);
    case bg3_granny_dt_transform:
      return sizeof(bg3_granny_transform);
    case bg3_granny_dt_float:
      return sizeof(float);
    case bg3_granny_dt_int8:
      return sizeof(int8_t);
    case bg3_granny_dt_uint8:
      return sizeof(uint8_t);
    case bg3_granny_dt_binormal_int8:
      return sizeof(int8_t);
    case bg3_granny_dt_normal_uint8:
      return sizeof(uint8_t);
    case bg3_granny_dt_int16:
      return sizeof(int16_t);
    case bg3_granny_dt_uint16:
      return sizeof(uint16_t);
    case bg3_granny_dt_binormal_int16:
      return sizeof(int16_t);
    case bg3_granny_dt_normal_uint16:
      return sizeof(uint16_t);
    case bg3_granny_dt_int32:
      return sizeof(int32_t);
    case bg3_granny_dt_uint32:
      return sizeof(uint32_t);
    case bg3_granny_dt_half:
      return sizeof(uint16_t);
    // case bg3_granny_dt_empty_reference:
    // return sizeof(void*);
    default:
      bg3_panic("invalid granny data type");
  }
}

static size_t granny_field_size(bg3_granny_type_info* ti);

static size_t granny_object_size(bg3_granny_type_info* ti) {
  size_t object_size = 0;
  for (bg3_granny_type_info* rti = ti; rti->type != bg3_granny_dt_end; ++rti) {
    object_size += granny_field_size(rti);
  }
  return object_size;
}

static size_t granny_field_size(bg3_granny_type_info* ti) {
  size_t field_size = granny_data_type_size(ti->type);
  if (ti->type == bg3_granny_dt_inline) {
    for (bg3_granny_type_info* rti = ti->reference_type; rti->type != bg3_granny_dt_end;
         ++rti) {
      field_size += granny_field_size(rti);
    }
  }
  if (ti->num_elements) {
    field_size *= ti->num_elements;
  }
  return field_size;
}

struct py_granny_span {
  py_granny_span(std::shared_ptr<py_granny_reader> reader,
                 bg3_granny_type_info* type_info,
                 void* data,
                 size_t num_elements)
      : reader(std::move(reader)),
        type_info(type_info),
        data(data),
        num_elements(num_elements) {}
  std::shared_ptr<py_granny_reader> reader;
  bg3_granny_type_info* type_info;
  void* data;
  size_t num_elements;
};

struct py_granny_direct_span : public py_granny_span {
  py_granny_direct_span(std::shared_ptr<py_granny_reader> reader,
                        bg3_granny_type_info* type_info,
                        void* data,
                        size_t num_elements)
      : py_granny_span(reader, type_info, data, num_elements),
        element_size(granny_object_size(type_info)) {}
  size_t element_size;
};

struct py_granny_ptr_span : public py_granny_span {
  py_granny_ptr_span(std::shared_ptr<py_granny_reader> reader,
                     bg3_granny_type_info* type_info,
                     void* data,
                     size_t num_elements)
      : py_granny_span(reader, type_info, data, num_elements) {}
};

static py::tuple convert_vec3(float vec[3]) {
  return py::make_tuple(vec[0], vec[1], vec[2]);
}
static py::tuple convert_vec4(float vec[4]) {
  return py::make_tuple(vec[0], vec[1], vec[2], vec[3]);
}

static py::object convert_scalar(std::shared_ptr<py_granny_reader>& reader,
                                 bg3_granny_type_info* ti,
                                 char* ptr,
                                 size_t offset);

struct py_granny_ptr {
  py_granny_ptr(std::shared_ptr<py_granny_reader> reader,
                bg3_granny_type_info* type_info,
                void* data)
      : reader(std::move(reader)), type_info(type_info), data(data) {}
  py::object getattr(std::string const& name) {
    size_t offset = 0;
    char* ptr = (char*)data;
    for (bg3_granny_type_info* ti = type_info; ti->type != bg3_granny_dt_end; ++ti) {
      if (!strcmp(name.c_str(), ti->name)) {
        if (ti->num_elements) {
          py::list rv;
          size_t element_size = granny_field_size(ti) / ti->num_elements;
          for (size_t i = 0; i < ti->num_elements; ++i) {
            rv.append(convert_scalar(reader, ti, ptr, offset));
            offset += element_size;
          }
          return rv;
        } else {
          return convert_scalar(reader, ti, ptr, offset);
        }
      }
      offset += granny_field_size(ti);
    }
    return py::none();
  }
  std::vector<std::string> dir() {
    std::vector<std::string> output;
    for (bg3_granny_type_info* ti = type_info; ti->type != bg3_granny_dt_end; ++ti) {
      output.push_back(ti->name);
    }
    return output;
  }
  std::shared_ptr<py_granny_reader> reader;
  bg3_granny_type_info* type_info;
  void* data;
};

static py::object convert_scalar(std::shared_ptr<py_granny_reader>& reader,
                                 bg3_granny_type_info* ti,
                                 char* ptr,
                                 size_t offset) {
  switch (ti->type) {
    case bg3_granny_dt_inline: {
      return py::cast(
          std::make_unique<py_granny_ptr>(reader, ti->reference_type, ptr + offset));
    }
    case bg3_granny_dt_reference: {
      void* ref;
      memcpy(&ref, ptr + offset, sizeof(void*));
      return py::cast(std::make_unique<py_granny_ptr>(reader, ti->reference_type, ref));
    }
    case bg3_granny_dt_reference_to_array: {
      struct LIBBG3_GRANNY_PACK {
        int32_t num_elements;
        void* ref;
      } pack;
      memcpy(&pack, ptr + offset, sizeof(pack));
      return py::cast(std::make_unique<py_granny_direct_span>(
          reader, ti->reference_type, pack.ref, pack.num_elements));
    }
    case bg3_granny_dt_array_of_references: {
      struct LIBBG3_GRANNY_PACK {
        int32_t num_elements;
        void* ref;
      } pack;
      memcpy(&pack, ptr + offset, sizeof(pack));
      return py::cast(std::make_unique<py_granny_ptr_span>(reader, ti->reference_type,
                                                           pack.ref, pack.num_elements));
    }
    case bg3_granny_dt_variant_reference: {
      bg3_granny_variant variant;
      memcpy(&variant, ptr + offset, sizeof(bg3_granny_variant));
      return py::cast(std::make_unique<py_granny_ptr>(reader, variant.type, variant.obj));
    }
    case bg3_granny_dt_reference_to_variant_array: {
      bg3_granny_variant_array variant_array;
      memcpy(&variant_array, ptr + offset, sizeof(bg3_granny_variant_array));
      return py::cast(std::make_unique<py_granny_ptr_span>(
          reader, variant_array.type, variant_array.items, variant_array.num_items));
    }
    case bg3_granny_dt_string: {
      char* str;
      memcpy(&str, ptr + offset, sizeof(char*));
      return py::str(str);
    }
    case bg3_granny_dt_transform: {
      bg3_granny_transform transform;
      memcpy(&transform, ptr + offset, sizeof(bg3_granny_transform));
      return py::make_tuple(
          transform.flags, convert_vec3(transform.position),
          convert_vec4(transform.orientation), convert_vec3(transform.scale_shear[0]),
          convert_vec3(transform.scale_shear[1]), convert_vec3(transform.scale_shear[2]));
    }
#define CONVERT_FLOAT(g_type, c_type)           \
  case bg3_granny_dt_##g_type: {                \
    c_type val;                                 \
    memcpy(&val, ptr + offset, sizeof(c_type)); \
    return py::float_(val);                     \
  }
#define CONVERT_INT(g_type, c_type)             \
  case bg3_granny_dt_##g_type: {                \
    c_type val;                                 \
    memcpy(&val, ptr + offset, sizeof(c_type)); \
    return py::int_(val);                       \
  }
      CONVERT_FLOAT(float, float);
      CONVERT_INT(int8, int8_t);
      CONVERT_INT(uint8, uint8_t);
      CONVERT_INT(binormal_int8, int8_t);
      CONVERT_INT(normal_uint8, uint8_t);
      CONVERT_INT(int16, int16_t);
      CONVERT_INT(uint16, uint16_t);
      CONVERT_INT(binormal_int16, int16_t);
      CONVERT_INT(normal_uint16, uint16_t);
      CONVERT_INT(int32, int32_t);
      CONVERT_INT(uint32, uint32_t);
#undef CONVERT_FLOAT
#undef CONVERT_INT
    case bg3_granny_dt_half: {
      bg3_half val;
      memcpy(&val, ptr + offset, sizeof(uint16_t));
      return py::float_((float)val);
    }
    default:
      return py::none();
  }
}

struct py_granny_reader : public std::enable_shared_from_this<py_granny_reader> {
  static std::shared_ptr<py_granny_reader> from_path(py::str path) {
    return std::make_shared<py_granny_reader>(path);
  }
  static std::shared_ptr<py_granny_reader> from_data(py::bytes data) {
    return std::make_shared<py_granny_reader>(data);
  }
  py_granny_reader(py::str py_path) {
    std::string path(py_path);
    bg3_status status = bg3_mapped_file_init_ro(&mapped, path.c_str());
    if (status) {
      throw std::runtime_error("Failed to open gr2 file");
    }
    status =
        bg3_granny_reader_init(&reader, mapped.data, mapped.data_len, &pybg3_granny_ops);
    if (status) {
      bg3_mapped_file_destroy(&mapped);
      throw std::runtime_error("Failed to parse gr2 file");
    }
    is_mapped_file = true;
  }
  py_granny_reader(py::bytes data) : data(data) {
    std::string_view view(data);
    // TODO: reader isn't const correct. This one is actually quite bad because the granny
    // reader will actually modify data to apply its pointer fixups. We need to make it
    // always copy on write.
    bg3_status status = bg3_granny_reader_init(&reader, const_cast<char*>(view.data()),
                                               view.size(), &pybg3_granny_ops);
    if (status) {
      throw std::runtime_error("Failed to parse gr2 file");
    }
  }
  ~py_granny_reader() {
    if (is_mapped_file) {
      bg3_mapped_file_destroy(&mapped);
    }
    bg3_granny_reader_destroy(&reader);
  }
  std::unique_ptr<py_granny_ptr> root() {
    bg3_granny_obj_root* root = bg3_granny_reader_get_root(&reader);
    bg3_granny_type_info* root_type = bg3_granny_reader_get_root_type(&reader);
    return std::make_unique<py_granny_ptr>(shared_from_this(), root_type, root);
  }
  bool is_mapped_file{false};
  py::bytes data;
  bg3_mapped_file mapped;
  bg3_granny_reader reader;
};

void pybg3_log(std::string const& message) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("%s\n", message.c_str());
}

PYBIND11_MODULE(_pybg3, m) {
  m.doc() = "python libbg3 bindings";
  m.def("osiris_compile_path", &osiris_compile_path, "Compile an osiris save");
  m.def("osiris_decompile_path", &osiris_decompile_path, "Decompile an osiris save");
  m.def("log", &pybg3_log, "Log a message");
  py::class_<py_lspk_file>(m, "_LspkFile")
      .def(py::init<const std::string&>())
      .def("file_name", &py_lspk_file::file_name)
      .def("file_size", &py_lspk_file::file_size)
      .def("file_data", &py_lspk_file::file_data)
      .def("num_files", &py_lspk_file::num_files)
      .def("priority", &py_lspk_file::priority);
  py::class_<py_lsof_file>(m, "_LsofFile")
      .def_static("from_path", &py_lsof_file::from_path)
      .def_static("from_data", &py_lsof_file::from_data)
      .def("is_wide", &py_lsof_file::is_wide)
      .def("ensure_sibling_pointers", &py_lsof_file::ensure_sibling_pointers)
      .def("num_nodes", &py_lsof_file::num_nodes)
      .def("num_attrs", &py_lsof_file::num_attrs)
      .def("node", &py_lsof_file::node)
      .def("attr", &py_lsof_file::attr)
      .def("stats", &py_lsof_file::stats)
      .def("scan_unique_objects", &py_lsof_file::scan_unique_objects)
      .def("to_sexp", &py_lsof_file::to_sexp);
  py::class_<py_loca_file>(m, "_LocaFile")
      .def_static("from_path", &py_loca_file::from_path)
      .def_static("from_data", &py_loca_file::from_data)
      .def("num_entries", &py_loca_file::num_entries)
      .def("entry", &py_loca_file::entry);
  py::class_<py_index_reader>(m, "_IndexReader")
      .def(py::init<const std::string&>())
      .def("query", &py_index_reader::query);
  py::class_<py_granny_reader, std::shared_ptr<py_granny_reader>>(m, "_GrannyReader")
      .def_static("from_path", &py_granny_reader::from_path)
      .def_static("from_data", &py_granny_reader::from_data)
      .def_property_readonly("root", &py_granny_reader::root);
  py::class_<py_granny_ptr>(m, "_GrannyPtr")
      .def("__getattr__", &py_granny_ptr::getattr, py::is_operator())
      .def("__dir__", &py_granny_ptr::dir, py::is_operator());
  py::class_<py_granny_direct_span>(m, "_GrannyDirectSpan");
  py::class_<py_granny_ptr_span>(m, "_GrannyPtrSpan");
}
