#!/bin/bash
if [ ! -e tmp/good.txt ]; then
  echo Doing good run...
  OG_GRANNY="/Users/eiz/l/bg3/MacOS/Baldur's Gate 3" lldb \
    --source scripts/bk_trace.lldb -- build/pybg3_test \
    --gtest_filter=PyBg3GrannyTest.PyBg3GrannyTestFile > tmp/good.txt
fi
if [ ! -e tmp/bad.txt ]; then
  echo Doing bad run...
  lldb --source scripts/bk_trace.lldb -- build/pybg3_test \
    --gtest_filter=PyBg3GrannyTest.PyBg3GrannyTestFile > tmp/bad.txt
fi
