#include "pybg3_granny.h"
#include "rans.h"

// TODO: the current rANS decoder does not support an incremental API, but neither does
// libbg3 actually use the incremental api incrementally, we just want ABI compatibility
// with the real granny functions. For now we just blind assume the entire data is in
// one decompress call.
void* pybg3_granny_begin_file_decompression(int type,
                                            bool endian_swapped,
                                            uint32_t uncompressed_size,
                                            void* uncompressed_data,
                                            uint32_t buf_size,
                                            void* buffer) {
  if (type != bg3_granny_compression_bitknit2 || endian_swapped) {
    return nullptr;
  }
  return new rans::bitknit2_state({(uint8_t*)uncompressed_data, uncompressed_size});
}

bool pybg3_granny_decompress_incremental(void* context,
                                         uint32_t compressed_size,
                                         void* compressed_data) {
  rans::bitknit2_state* ctx = (rans::bitknit2_state*)context;
  try {
    bool result = ctx->decode({(uint16_t*)compressed_data, compressed_size / 2});
    return result;
  } catch (std::exception& e) {
    bg3_error("Decompression error: %s\n", e.what());
    return false;
  }
}

bool pybg3_granny_end_file_decompression(void* context) {
  delete (rans::bitknit2_state*)context;
  return true;
}

const bg3_granny_compressor_ops pybg3_granny_ops = {
    .begin_file_decompression = pybg3_granny_begin_file_decompression,
    .end_file_decompression = pybg3_granny_end_file_decompression,
    .decompress_incremental = pybg3_granny_decompress_incremental,
};
