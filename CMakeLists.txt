cmake_minimum_required(VERSION 3.28..3.28)
project(${SKBUILD_PROJECT_NAME} LANGUAGES C CXX)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)
add_library(libbg3_third_party STATIC
  third_party/libbg3/third_party/lz4.c
  third_party/libbg3/third_party/lz4frame.c
  third_party/libbg3/third_party/lz4hc.c
  third_party/libbg3/third_party/miniz.c
  third_party/libbg3/third_party/xxhash.c)
target_include_directories(libbg3_third_party PUBLIC third_party/libbg3/third_party)
python_add_library(_pybg3 MODULE src/pybg3.cc WITH_SOABI)
target_include_directories(_pybg3 PRIVATE third_party/libbg3)
target_link_libraries(_pybg3 PRIVATE libbg3_third_party pybind11::headers)
install(TARGETS _pybg3 DESTINATION ${SKBUILD_PROJECT_NAME})
