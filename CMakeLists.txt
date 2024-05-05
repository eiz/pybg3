cmake_minimum_required(VERSION 3.28..3.28)
project(${SKBUILD_PROJECT_NAME} LANGUAGES C CXX)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)
add_library(libbg3_third_party STATIC
  third_party/libbg3/third_party/lz4.c
  third_party/libbg3/third_party/lz4frame.c
  third_party/libbg3/third_party/lz4hc.c
  third_party/libbg3/third_party/miniz.c
  third_party/libbg3/third_party/xxhash.c)
target_include_directories(libbg3_third_party PUBLIC third_party/libbg3/third_party)
python_add_library(_pybg3 MODULE src/pybg3.cc src/pybg3_granny.cc WITH_SOABI)
target_include_directories(_pybg3 PRIVATE third_party/libbg3)
target_link_libraries(_pybg3 PRIVATE libbg3_third_party pybind11::headers)
add_executable(pybg3_test src/pybg3_granny.cc src/rans_test.cc src/pybg3_granny_test.cc)
target_include_directories(pybg3_test PRIVATE third_party/libbg3)
target_link_libraries(pybg3_test PRIVATE libbg3_third_party GTest::gtest_main)
target_link_options(pybg3_test PRIVATE)
install(TARGETS _pybg3 DESTINATION ${SKBUILD_PROJECT_NAME})
