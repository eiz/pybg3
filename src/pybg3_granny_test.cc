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
#include "pybg3_granny.h"

#include <stdexcept>
#include <string>

#include "libbg3.h"
#include "rans.h"

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
  struct timespec start, end;
  double shortest = 1000000.0;
  for (int i = 0; i < 100; ++i) {
    clock_gettime(CLOCK_MONOTONIC, &start);
    bg3_granny_reader reader;
    if (bg3_granny_reader_init(&reader, mapped.data, mapped.data_len, &compress_ops)) {
      throw std::runtime_error("Failed to initialize granny reader");
    }
    bg3_granny_reader_destroy(&reader);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double start_ns = start.tv_sec * 1e9 + start.tv_nsec;
    double end_ns = end.tv_sec * 1e9 + end.tv_nsec;
    double elapsed = (end_ns - start_ns) / 1e9;
    if (elapsed < shortest) {
      shortest = elapsed;
    }
  }
  bg3_mapped_file_destroy(&mapped);
  printf("Shortest time: %f\n", shortest);
}

// Is this the rANS encoder that does the least possible amount of compression?
// The compression ratio is given by the binary entropy function:
// https://en.wikipedia.org/wiki/Binary_entropy_function
TEST(PyBg3GrannyTest, Zen) {
  rans::rans_state<uint64_t> state;
  rans::frequency_table<uint32_t, 2, 31, 1> cdf;
  bg3_mapped_file mapped;
  char const* path = "/tmp/thefile";
  if (bg3_mapped_file_init_ro(&mapped, path)) {
    throw std::runtime_error("Failed to open file");
  }
  uint64_t num_one_bits = 0;
  uint8_t* data = (uint8_t*)mapped.data;
  for (size_t i = 0; i < mapped.data_len; ++i) {
    num_one_bits += __builtin_popcount(data[i]);
  }
  uint64_t num_zero_bits = mapped.data_len * 8ULL - num_one_bits;
  uint32_t zero_freq = num_zero_bits * 0x7FFFFFFFULL / (num_zero_bits + num_one_bits);
  cdf.sums = {0ULL, zero_freq, 0x80000000ULL};
  cdf.finish_update();
  size_t bitbuf_len = mapped.data_len / 4 + 3;
  uint32_t* bitbuf = new uint32_t[bitbuf_len];
  rans::bounded_stack<uint32_t> bitstream(bitbuf, bitbuf + bitbuf_len,
                                          bitbuf + bitbuf_len);
  for (size_t i = 0; i < mapped.data_len; ++i) {
    for (int j = 0; j < 8; ++j) {
      state.push_cdf(bitstream, (data[i] >> j) & 1, cdf);
    }
  }
  bitstream.push(state.bits & 0xFFFFFFFFU);
  bitstream.push(state.bits >> 32);
  FILE* fp = fopen("/tmp/thefile.rans", "wb");
  fwrite(bitstream.cur, 4, bitstream.end - bitstream.cur, fp);
  fclose(fp);
  state.bits = 0;
  state.bits |= (uint64_t)bitstream.pop() << 32;
  state.bits |= bitstream.pop();
  for (size_t i = mapped.data_len - 1; i; --i) {
    for (int j = 7; j >= 0; --j) {
      EXPECT_EQ((data[i] >> j) & 1, state.pop_cdf(bitstream, cdf));
    }
  }
  delete[] bitbuf;
}
