cmake_minimum_required(VERSION 3.26)
project(mona_lyra2re2_r1 LANGUAGES C CXX)
set(MONA_SM "89" CACHE STRING "Native GPU target: 89 or 120")
set(MONA_UPSTREAM "" CACHE PATH "Clean ccminer windows@6ff4e50987e59a70056324a94ed8667cc0bf598d checkout")
set(MONA_MAX_REGISTERS "0" CACHE STRING "Experimental global register cap; 0 means no cap")
option(MONA_LYRA_ACTIVE_MASK "Use __activemask() for 4-lane Lyra shuffles (performance reference)" OFF)
option(MONA_LYRA_STABLE_BALLOT "Capture a stable live-lane ballot once per Lyra cooperative kernel" OFF)
option(MONA_LYRA_FULL_MASK "Use full-warp membership in Lyra shuffle paths (experimental)" OFF)
option(MONA_LYRA_DUMMY_TAIL "Keep tail warp lanes participating with dummy Lyra state; requires FULL_MASK" OFF)
option(MONA_LYRA_STATIC_SHARED64 "Specialize shared addressing for block=(4,16) only (experimental)" OFF)
option(MONA_LYRA_PREFETCH_ROWOUT "Prefetch Lyra rowOut shared data before round_lyra_v5 (experimental)" OFF)
option(MONA_LYRA_PREFETCH_NEXTCOL "Software-pipeline next-column rowIn/rowInOut shared loads in normal wandering (experimental)" OFF)
option(MONA_LYRA_ROW3_FIRSTREAD "Carry setup row3 in registers into the first wandering read (experimental)" OFF)
set(MONA_CUBE_IMAD_PHASE_MASK "0" CACHE STRING "CubeHash IMAD phase mask (SM120 experimental): bit0..bit3 select the four 16-add phases")
option(MONA_CPU_ONLY "Build only the CPU reference library" OFF)
if(NOT MONA_CUBE_IMAD_PHASE_MASK MATCHES "^(0|[1-9]|1[0-5])$")
  message(FATAL_ERROR "MONA_CUBE_IMAD_PHASE_MASK must be an integer from 0 through 15")
endif()
if(NOT MONA_CUBE_IMAD_PHASE_MASK STREQUAL "0" AND NOT MONA_SM STREQUAL "120")
  message(FATAL_ERROR "MONA_CUBE_IMAD_PHASE_MASK is an SM120-only experiment")
endif()
if(MONA_LYRA_ACTIVE_MASK AND MONA_LYRA_STABLE_BALLOT)
  message(FATAL_ERROR "MONA_LYRA_ACTIVE_MASK and MONA_LYRA_STABLE_BALLOT are mutually exclusive")
endif()
if(MONA_LYRA_ACTIVE_MASK AND MONA_LYRA_FULL_MASK)
  message(FATAL_ERROR "MONA_LYRA_ACTIVE_MASK and MONA_LYRA_FULL_MASK are mutually exclusive")
endif()
if(MONA_LYRA_STABLE_BALLOT AND MONA_LYRA_FULL_MASK)
  message(FATAL_ERROR "MONA_LYRA_STABLE_BALLOT and MONA_LYRA_FULL_MASK are mutually exclusive")
endif()
if(MONA_LYRA_DUMMY_TAIL AND NOT MONA_LYRA_FULL_MASK)
  message(FATAL_ERROR "MONA_LYRA_DUMMY_TAIL requires MONA_LYRA_FULL_MASK=ON")
endif()
if(MONA_LYRA_DUMMY_TAIL AND (MONA_LYRA_ACTIVE_MASK OR MONA_LYRA_STABLE_BALLOT))
  message(FATAL_ERROR "MONA_LYRA_DUMMY_TAIL is exclusive with ACTIVE_MASK and STABLE_BALLOT")
endif()
if(NOT MONA_SM STREQUAL "89" AND NOT MONA_SM STREQUAL "120")
  message(FATAL_ERROR "Only SM89 and SM120 are supported")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "64-bit build required; Win32 is not supported")
endif()
if(NOT EXISTS "${MONA_UPSTREAM}/lyra2/lyra2REv2.cu")
  message(FATAL_ERROR "Run tools/bootstrap.ps1 or set MONA_UPSTREAM to the pinned checkout")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MONA_UPSTREAM}" rev-parse HEAD
  OUTPUT_VARIABLE base OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT base STREQUAL "6ff4e50987e59a70056324a94ed8667cc0bf598d")
  message(FATAL_ERROR "Wrong upstream revision: ${base}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MONA_UPSTREAM}" status --porcelain
  OUTPUT_VARIABLE dirty COMMAND_ERROR_IS_FATAL ANY)
if(NOT dirty STREQUAL "")
  message(FATAL_ERROR "Upstream checkout must be clean; this project never patches it in place")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(mona_reference STATIC
  src/reference.cpp
  "${MONA_UPSTREAM}/sph/blake.c" "${MONA_UPSTREAM}/sph/keccak.c"
  "${MONA_UPSTREAM}/sph/cubehash.c" "${MONA_UPSTREAM}/sph/skein.c"
  "${MONA_UPSTREAM}/sph/bmw.c"
  "${MONA_UPSTREAM}/lyra2/Lyra2.c" "${MONA_UPSTREAM}/lyra2/Sponge.c")
target_include_directories(mona_reference PUBLIC include PRIVATE "${MONA_UPSTREAM}")
if(MSVC)
  target_compile_definitions(mona_reference PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(NOT MONA_CPU_ONLY)
  enable_language(CUDA)
  if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 12.8)
    message(FATAL_ERROR "Use CUDA >=12.8 (native SM120); compare toolkit versions by measurement")
  endif()
  find_package(CUDAToolkit REQUIRED)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_identity.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare.py"
    --upstream "${MONA_UPSTREAM}" --out "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_identity.py"
    --project-root "${CMAKE_CURRENT_SOURCE_DIR}"
    --upstream "${MONA_UPSTREAM}"
    --generated "${CMAKE_CURRENT_BINARY_DIR}/generated"
    --out-json "${CMAKE_CURRENT_BINARY_DIR}/mona-build-identity.json"
    --out-header "${CMAKE_CURRENT_BINARY_DIR}/generated/mona_build_identity.hpp"
    --setting "MONA_SM=${MONA_SM}"
    --setting "MONA_MAX_REGISTERS=${MONA_MAX_REGISTERS}"
    --setting "MONA_LYRA_ACTIVE_MASK=${MONA_LYRA_ACTIVE_MASK}"
    --setting "MONA_LYRA_STABLE_BALLOT=${MONA_LYRA_STABLE_BALLOT}"
    --setting "MONA_LYRA_FULL_MASK=${MONA_LYRA_FULL_MASK}"
    --setting "MONA_LYRA_DUMMY_TAIL=${MONA_LYRA_DUMMY_TAIL}"
    --setting "MONA_LYRA_PREFETCH_ROWOUT=${MONA_LYRA_PREFETCH_ROWOUT}"
    --setting "MONA_LYRA_PREFETCH_NEXTCOL=${MONA_LYRA_PREFETCH_NEXTCOL}"
    --setting "MONA_LYRA_STATIC_SHARED64=${MONA_LYRA_STATIC_SHARED64}"
    --setting "MONA_LYRA_ROW3_FIRSTREAD=${MONA_LYRA_ROW3_FIRSTREAD}"
    --setting "MONA_CUBE_IMAD_PHASE_MASK=${MONA_CUBE_IMAD_PHASE_MASK}"
    --cuda-compiler "${CMAKE_CUDA_COMPILER}"
    --cuda-version "${CMAKE_CUDA_COMPILER_VERSION}"
    --cxx-compiler "${CMAKE_CXX_COMPILER}"
    --cxx-id "${CMAKE_CXX_COMPILER_ID}"
    --cxx-version "${CMAKE_CXX_COMPILER_VERSION}"
    --cmake-version "${CMAKE_VERSION}"
    --generator "${CMAKE_GENERATOR}"
    --build-type "${CMAKE_BUILD_TYPE}"
    COMMAND_ERROR_IS_FATAL ANY)
  add_library(mona_core STATIC src/pipeline.cu src/miner.cpp src/build_identity.cpp)
  target_include_directories(mona_core PUBLIC include PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" "${MONA_UPSTREAM}")
  target_link_libraries(mona_core PUBLIC mona_reference CUDA::cudart)
  target_compile_definitions(mona_core PRIVATE MONA_TARGET_SM=${MONA_SM})
  target_compile_definitions(mona_core PRIVATE MONA_CUBE_IMAD_PHASE_MASK=${MONA_CUBE_IMAD_PHASE_MASK})
  if(MONA_LYRA_FULL_MASK)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_FULL_MASK=1)
  endif()
  if(MONA_LYRA_DUMMY_TAIL)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_DUMMY_TAIL=1)
  endif()
  if(MONA_LYRA_STABLE_BALLOT)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_STABLE_BALLOT=1)
  endif()
  if(MONA_LYRA_STATIC_SHARED64)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_STATIC_SHARED64=1)
  endif()
  if(MONA_LYRA_ACTIVE_MASK)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_ACTIVE_MASK=1)
  endif()
  if(MONA_LYRA_PREFETCH_ROWOUT)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_PREFETCH_ROWOUT=1)
  endif()
  if(MONA_LYRA_PREFETCH_NEXTCOL)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_PREFETCH_NEXTCOL=1)
  endif()
  if(MONA_LYRA_ROW3_FIRSTREAD)
    target_compile_definitions(mona_core PRIVATE MONA_LYRA_ROW3_FIRSTREAD=1)
  endif()
  set_target_properties(mona_core PROPERTIES CUDA_ARCHITECTURES "${MONA_SM}-real"
    CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED YES CUDA_SEPARABLE_COMPILATION OFF)
  target_compile_options(mona_core PRIVATE "$<$<COMPILE_LANGUAGE:CUDA>:-O3;-lineinfo;-Xptxas=-v,-warn-spills>")
  if(NOT MONA_MAX_REGISTERS STREQUAL "0")
    target_compile_options(mona_core PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--maxrregcount=${MONA_MAX_REGISTERS}>)
  endif()
  if(MSVC)
    target_compile_options(mona_core PRIVATE "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/EHsc>")
  endif()
  add_executable(mona_bench src/bench.cpp)
  target_link_libraries(mona_bench PRIVATE mona_core)
  set_target_properties(mona_bench PROPERTIES OUTPUT_NAME "mona_bench_sm${MONA_SM}")
endif()
