cmake_minimum_required(VERSION 3.16)
project(nsmb_dsi_harness C CXX)

# Stock melonDS, pinned by tools/bootstrap.sh. Only the emulator core is built.
set(MELONDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.deps/melonDS" CACHE PATH "melonDS source tree")
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
add_subdirectory(${MELONDS_DIR} melonDS EXCLUDE_FROM_ALL)

add_executable(harness main.cpp platform.cpp)
target_compile_features(harness PRIVATE cxx_std_20)
target_include_directories(harness PRIVATE
	${MELONDS_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/../../modules/dsi/include)
target_link_libraries(harness PRIVATE core)
