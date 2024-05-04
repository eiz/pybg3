cmake_minimum_required(VERSION 3.28..3.28)
project(${SKBUILD_PROJECT_NAME} LANGUAGES C CXX)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
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
python_add_library(_pybg3 MODULE src/pybg3.cc WITH_SOABI)
target_include_directories(_pybg3 PRIVATE third_party/libbg3)
target_link_libraries(_pybg3 PRIVATE libbg3_third_party pybind11::headers)
add_executable(pybg3_test src/rans_test.cc)
target_link_libraries(pybg3_test PRIVATE GTest::gtest_main)
install(TARGETS _pybg3 DESTINATION ${SKBUILD_PROJECT_NAME})
