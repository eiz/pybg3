// libbg3 examples
//
// Copyright (C) 2024 Mackenzie Straight.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.

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

PYBIND11_MODULE(_pybg3, m) {
  m.doc() = "python libbg3 bindings";
  m.def("osiris_compile_path", &osiris_compile_path, "Compile an osiris save");
  m.def("osiris_decompile_path", &osiris_decompile_path, "Decompile an osiris save");
}
