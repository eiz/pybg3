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

#include "rans.h"

#include <random>

#include <gtest/gtest.h>

using namespace rans;

TEST(RansTest, RansStateSymmetric) {
  std::array<uint16_t, 128> bitbuf;
  rans_bitstream<uint16_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  rans_state<uint32_t> state;
  for (int i = 0; i < 256; ++i) {
    state.push_bits(bitstream, i, 8);
  }
  EXPECT_EQ(bitstream.begin, bitstream.cur);
  for (int i = 255; i >= 0; --i) {
    EXPECT_EQ(i, state.pop_bits(bitstream, 8));
  }
  EXPECT_EQ(bitstream.end, bitstream.cur);
  EXPECT_EQ(state.bits, 0x10000);
}

TEST(RansTest, RansStateBitstreamOverflows) {
  std::array<uint16_t, 128> bitbuf;
  rans_bitstream<uint16_t> bitstream(bitbuf.begin(), bitbuf.begin(), bitbuf.end());
  EXPECT_THROW(bitstream.push(0), std::out_of_range);
  bitstream = rans_bitstream<uint16_t>(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  EXPECT_THROW(bitstream.pop(), std::out_of_range);
}

TEST(RansTest, RansStateCdf) {
  std::array<uint16_t, 128> bitbuf;
  rans_bitstream<uint16_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  rans_state<uint32_t> state;
  frequency_table<uint16_t, 2, 15> table;
  table.sums = {0, 0x6000, 0x8000};
  // Test with ones, which will take more space to store due to 1/4 probability.
  EXPECT_EQ(state.bits, 0x10000);
  state.push_cdf(bitstream, 0, table);
  for (int i = 0; i < 10; ++i) {
    state.push_cdf(bitstream, 1, table);
  }
  EXPECT_EQ(bitstream.cur, bitstream.end - 1);
  rans_state<uint32_t> ones_state = state;
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(1, state.pop_cdf(bitstream, table));
  }
  EXPECT_EQ(0, state.pop_cdf(bitstream, table));
  EXPECT_EQ(bitstream.cur, bitstream.end);
  // Test with zeroes, which won't offload any bits due to 3/4 probability.
  state.push_cdf(bitstream, 1, table);
  for (int i = 0; i < 10; ++i) {
    state.push_cdf(bitstream, 0, table);
  }
  EXPECT_EQ(bitstream.cur, bitstream.end);
  rans_state<uint32_t> zeros_state = state;
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(0, state.pop_cdf(bitstream, table));
  }
  EXPECT_EQ(1, state.pop_cdf(bitstream, table));
  EXPECT_EQ(bitstream.cur, bitstream.end);
  int num_zero_pushed = 0;
  while (bitstream.cur == bitstream.end) {
    state.push_cdf(bitstream, 0, table);
    ++num_zero_pushed;
  }
  for (int i = 0; i < num_zero_pushed; ++i) {
    EXPECT_EQ(0, state.pop_cdf(bitstream, table));
  }
  int num_ones_pushed = 0;
  while (bitstream.cur == bitstream.end) {
    state.push_cdf(bitstream, 1, table);
    ++num_ones_pushed;
  }
  for (int i = 0; i < num_ones_pushed; ++i) {
    EXPECT_EQ(1, state.pop_cdf(bitstream, table));
  }
  EXPECT_EQ(bitstream.cur, bitstream.end);
  printf("Ones overflowed at %d, zeros overflowed at %d\n", num_ones_pushed,
         num_zero_pushed);
}

TEST(RansTest, RansStateCdf64) {
  std::array<uint32_t, 128> bitbuf;
  rans_bitstream<uint32_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  rans_state<uint64_t> state;
  frequency_table<uint32_t, 2, 31> table;
  table.sums = {0, 0x7FFF0000, 0x80000000};
  int64_t num_zero_pushed = 0;
  while (bitstream.cur == bitstream.end && num_zero_pushed < 1000000) {
    state.push_cdf(bitstream, 0, table);
    ++num_zero_pushed;
  }
  for (int i = 0; i < num_zero_pushed; ++i) {
    EXPECT_EQ(0, state.pop_cdf(bitstream, table));
  }
  int64_t num_ones_pushed = 0;
  while (bitstream.cur == bitstream.end) {
    state.push_cdf(bitstream, 1, table);
    ++num_ones_pushed;
  }
  for (int i = 0; i < num_ones_pushed; ++i) {
    EXPECT_EQ(1, state.pop_cdf(bitstream, table));
  }
  EXPECT_EQ(bitstream.cur, bitstream.end);
  printf("Ones overflowed at %lld, zeros overflowed at %lld\n", num_ones_pushed,
         num_zero_pushed);
}

TEST(RansTest, RansPushBitsOffload) {
  rans_state<uint32_t> state;
  std::array<uint16_t, 128> bitbuf;
  rans_bitstream<uint16_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  int num_pushed = 0;
  while (bitstream.cur == bitstream.end) {
    state.push_bits(bitstream, 0, 1);
    ++num_pushed;
  }
  for (int i = 0; i < num_pushed; ++i) {
    EXPECT_EQ(0, state.pop_bits(bitstream, 1));
  }
  EXPECT_EQ(bitstream.cur, bitstream.end);
  EXPECT_EQ(num_pushed, 16);
}

TEST(RansTest, RansPushCdfOffload) {
  using model_t = deferred_adaptive_model<uint16_t, 1024, 256, 192, 15>;
  std::array<uint8_t, 128> random_values;
  std::mt19937 rng(12345);
  std::uniform_int_distribution<uint8_t> dist(0, 63);
  for (auto& value : random_values) {
    value = dist(rng);
  }
  rans_state<uint32_t> state;
  std::array<uint16_t, 128> bitbuf;
  rans_bitstream<uint16_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  model_t model;
  for (auto value : random_values) {
    state.push_cdf(bitstream, value, model.cdf);
  }
  EXPECT_EQ(48, bitstream.end - bitstream.cur);  // ~6 bits/symbol
  for (size_t i = random_values.size(); i > 0; --i) {
    EXPECT_EQ(random_values[i - 1], state.pop_cdf(bitstream, model.cdf));
  }
}

TEST(RansTest, RansPushCdfOffload64) {
  using model_t = deferred_adaptive_model<uint32_t, 1024, 256, 192, 15>;
  std::array<uint8_t, 128> random_values;
  std::mt19937 rng(12345);
  std::uniform_int_distribution<uint8_t> dist(0, 63);
  for (auto& value : random_values) {
    value = dist(rng);
  }
  rans_state<uint64_t> state;
  std::array<uint32_t, 128> bitbuf;
  rans_bitstream<uint32_t> bitstream(bitbuf.begin(), bitbuf.end(), bitbuf.end());
  model_t model;
  for (auto value : random_values) {
    state.push_cdf(bitstream, value, model.cdf);
  }
  EXPECT_EQ(24, bitstream.end - bitstream.cur);  // ~6 bits/symbol
  for (size_t i = random_values.size(); i > 0; --i) {
    EXPECT_EQ(random_values[i - 1], state.pop_cdf(bitstream, model.cdf));
  }
}

TEST(RansTest, RansSymFreqLast) {
  using model_t = deferred_adaptive_model<uint16_t, 1024, 300, 36, 15>;
  model_t model;
  EXPECT_EQ(*model.cdf.sums.rbegin(), model_t::total_sum);
  for (int converge_step = 0; converge_step < 15; ++converge_step) {
    for (size_t i = 0; i < model_t::adaptation_interval; ++i) {
      model.observe_symbol(299);
    }
  }
  EXPECT_EQ(model_t::last_frequency_incr, 725);
  EXPECT_EQ(*model.cdf.sums.rbegin(), model_t::total_sum);
  EXPECT_EQ(model_t::frequency_incr, 31);
  EXPECT_EQ(model.cdf.frequency(264), 1);
  uint32_t sum = 0;
  for (size_t i = 0; i < 300; ++i) {
    sum += model.cdf.frequency(i);
  }
  EXPECT_EQ(sum, model_t::total_sum);
  // The +1 here is due to the way rounding is done when updating the CDF.
  EXPECT_EQ(model.cdf.frequency(299) + 1,
            model_t::last_frequency_incr + 1 +
                model_t::frequency_incr * model_t::adaptation_interval);
}

TEST(RansTest, RegisterLruCache) {
  register_lru_cache<uint32_t> cache;
  cache.insert(42);
  EXPECT_EQ(42, cache.entry(6));
  cache.insert(420);
  EXPECT_EQ(420, cache.entry(6));
  EXPECT_EQ(42, cache.hit(7));
  EXPECT_EQ(42, cache.entry(0));
  EXPECT_EQ(420, cache.entry(7));
}
