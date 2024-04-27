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

#define LIBBG3_IMPLEMENTATION
#include "libbg3.h"

#include <pybind11/pybind11.h>

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
  py::bytes file_data(size_t idx) {
    if (idx >= lspk.num_files) {
      throw std::runtime_error("Index out of bounds");
    }
    std::string buf(lspk.manifest[idx].uncompressed_size, 0);
    size_t size = file_size(idx);
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

struct py_lsof_file {
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
  py_lsof_file(py::bytes data) {
    std::string_view view(data);
    bg3_status status = bg3_lsof_reader_init(&reader, (char*)view.data(), view.size());
    if (status) {
      bg3_mapped_file_destroy(&mapped);
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
  bool is_mapped_file{false};
  bg3_mapped_file mapped;
  bg3_lsof_reader reader;
};

struct py_loca_file {
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
  py_loca_file(py::bytes data) {
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
  bg3_loca_reader reader;
};

PYBIND11_MODULE(_pybg3, m) {
  m.doc() = "python libbg3 bindings";
  m.def("osiris_compile_path", &osiris_compile_path, "Compile an osiris save");
  m.def("osiris_decompile_path", &osiris_decompile_path, "Decompile an osiris save");
  py::class_<py_lspk_file>(m, "_LspkFile")
      .def(py::init<const std::string&>())
      .def("file_name", &py_lspk_file::file_name)
      .def("file_size", &py_lspk_file::file_size)
      .def("file_data", &py_lspk_file::file_data)
      .def("num_files", &py_lspk_file::num_files);
  py::class_<py_lsof_file>(m, "_LsofFile")
      // this api seems pretty grody and easy to mess up (str vs bytes doing totally
      // different things)
      .def(py::init<py::bytes>())
      .def(py::init<py::str>())
      .def("to_sexp", &py_lsof_file::to_sexp);
  py::class_<py_loca_file>(m, "_LocaFile")
      .def(py::init<py::bytes>())
      .def(py::init<py::str>())
      .def("num_entries", &py_loca_file::num_entries)
      .def("entry", &py_loca_file::entry);
}
