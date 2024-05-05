#define LIBBG3_IMPLEMENTATION
#include "pybg3_granny.h"

#include <stdexcept>
#include <string>

#include "libbg3.h"

#include <gtest/gtest.h>

static const size_t offset_granny_begin_file_decompression = 0x516a38;
static const size_t offset_granny_decompress_incremental = 0x516a3c;
static const size_t offset_granny_end_file_decompression = 0x516a40;

#define GRANNY_OP(name) \
  .name = (bg3_fn_granny_##name*)((char*)info.dli_fbase + offset_granny_##name)

TEST(PyBg3GrannyTest, PyBg3GrannyTestFile) {
  bg3_granny_compressor_ops compress_ops = pybg3_granny_ops;
  char const* bg3_path = getenv("OG_GRANNY");
  if (bg3_path) {
    void* handle = dlopen(bg3_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
      throw std::runtime_error("couldn't find bg3\n");
    }
    void* ptr = dlsym(handle, "_ZN2ls9SingletonINS_11FileManagerEE5m_ptrE");
    Dl_info info;
    if (!ptr || !dladdr(ptr, &info)) {
      throw std::runtime_error("couldn't find an export (use _dyld_* fns instead lol)\n");
    }
    compress_ops = (bg3_granny_compressor_ops){0, GRANNY_OP(begin_file_decompression),
                                               GRANNY_OP(decompress_incremental),
                                               GRANNY_OP(end_file_decompression)};
  }
  std::string base_path = "/Users/eiz/code/bg3do/Data/Gustav";
  std::string test_path =
      "Generated/Public/GustavDev/Assets/HLOD/BGH_SteelWatchFoundry_B/HLOD_1_0_0_1.GR2";
  std::string full_path = base_path + "/" + test_path;
  bg3_mapped_file mapped;
  if (bg3_mapped_file_init_ro(&mapped, full_path.c_str())) {
    throw std::runtime_error("Failed to open file");
  }
  for (int i = 0; i < 10; ++i) {
    bg3_granny_reader reader;
    if (bg3_granny_reader_init(&reader, mapped.data, mapped.data_len, &compress_ops)) {
      throw std::runtime_error("Failed to initialize granny reader");
    }
    bg3_granny_reader_destroy(&reader);
  }
}
