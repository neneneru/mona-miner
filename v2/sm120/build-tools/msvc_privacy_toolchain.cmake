# Mona Miner MSVC privacy/reproducibility toolchain.
# Use this file at CMake configure time so the flags apply to compiler-identification
# objects as well as project targets.
if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "MSVC privacy toolchain requires a Windows host")
endif()

if(NOT DEFINED MONA2_PATHMAP_ROOT OR MONA2_PATHMAP_ROOT STREQUAL "")
  message(FATAL_ERROR "Set MONA2_PATHMAP_ROOT to the neutral staging root")
endif()

file(TO_CMAKE_PATH "${MONA2_PATHMAP_ROOT}" _mona2_pathmap_root)
if(_mona2_pathmap_root MATCHES "[;,]")
  message(FATAL_ERROR "MONA2_PATHMAP_ROOT must not contain ',' or ';'")
endif()

set(_mona2_privacy_flags "/experimental:deterministic /pathmap:${_mona2_pathmap_root}=s")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${_mona2_privacy_flags}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_mona2_privacy_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} /Brepro")
