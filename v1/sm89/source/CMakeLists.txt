cmake_minimum_required(VERSION 3.26)
project(mona_miner_sm89 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Mona Miner requires a 64-bit build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PHASEC_BUILD_MINER "Build the Windows CUDA miner" ${WIN32})
option(PHASEC_BUILD_TESTS "Build host/GPU tests" ON)

set(PHASEC_UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/pinned-upstream")
set(PHASEC_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/certified-backend")
set(PHASEC_HOST "${CMAKE_CURRENT_SOURCE_DIR}/phasec-host")

add_library(phasec_core STATIC
  "${PHASEC_HOST}/src/phasec_core.cpp"
  "${PHASEC_UPSTREAM}/sph/sph_sha2.c")
target_include_directories(phasec_core PUBLIC
  "${PHASEC_HOST}/include"
  "${PHASEC_UPSTREAM}")
if(MSVC)
  target_compile_definitions(phasec_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(PHASEC_BUILD_TESTS)
  include(CTest)
  add_executable(phasec_core_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/phasec_core_tests.cpp")
  target_link_libraries(phasec_core_tests PRIVATE phasec_core)
  add_test(NAME phasec_core_tests COMMAND phasec_core_tests)

  add_executable(phasec_console_ui_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/console_ui_tests.cpp")
  target_include_directories(phasec_console_ui_tests PRIVATE "${PHASEC_HOST}/include")
  target_link_libraries(phasec_console_ui_tests PRIVATE phasec_core)
  add_test(NAME phasec_console_ui_tests COMMAND phasec_console_ui_tests)
endif()

if(PHASEC_BUILD_MINER)
  if(NOT WIN32)
    message(FATAL_ERROR "Mona Miner pool host is intentionally Windows-only")
  endif()

  enable_language(CUDA)
  if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 13.4 OR
     CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13.5)
    message(FATAL_ERROR "SM89 acceptance build requires CUDA 13.4.x")
  endif()
  find_package(CUDAToolkit REQUIRED)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

  # Regenerate the CUDA extraction from the immutable pinned upstream snapshot and
  # bind the exact source/toolchain/profile into a new build identity.
  set(MONA_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PHASEC_BACKEND}/tools/prepare.py"
    --upstream "${PHASEC_UPSTREAM}" --out "${MONA_GENERATED}"
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PHASEC_BACKEND}/tools/build_identity.py"
    --project-root "${PHASEC_BACKEND}"
    --upstream "${PHASEC_UPSTREAM}"
    --generated "${MONA_GENERATED}"
    --out-json "${CMAKE_CURRENT_BINARY_DIR}/mona-build-identity.json"
    --out-header "${MONA_GENERATED}/mona_build_identity.hpp"
    --setting "MONA_SM=89"
    --setting "MONA_MAX_REGISTERS=0"
    --setting "MONA_LYRA_ACTIVE_MASK=OFF"
    --setting "MONA_LYRA_STABLE_BALLOT=ON"
    --setting "MONA_LYRA_FULL_MASK=OFF"
    --setting "MONA_LYRA_DUMMY_TAIL=OFF"
    --setting "MONA_LYRA_PREFETCH_ROWOUT=ON"
    --setting "MONA_LYRA_PREFETCH_NEXTCOL=OFF"
    --setting "MONA_LYRA_STATIC_SHARED64=OFF"
    --setting "MONA_LYRA_ROW3_FIRSTREAD=OFF"
    --setting "MONA_CUBE_IMAD_PHASE_MASK=0"
    --cuda-compiler "${CMAKE_CUDA_COMPILER}"
    --cuda-version "${CMAKE_CUDA_COMPILER_VERSION}"
    --cxx-compiler "${CMAKE_CXX_COMPILER}"
    --cxx-id "${CMAKE_CXX_COMPILER_ID}"
    --cxx-version "${CMAKE_CXX_COMPILER_VERSION}"
    --cmake-version "${CMAKE_VERSION}"
    --generator "${CMAKE_GENERATOR}"
    --build-type "${CMAKE_BUILD_TYPE}"
    COMMAND_ERROR_IS_FATAL ANY)

  add_library(mona_reference STATIC
    "${PHASEC_BACKEND}/src/reference.cpp"
    "${PHASEC_UPSTREAM}/sph/blake.c"
    "${PHASEC_UPSTREAM}/sph/keccak.c"
    "${PHASEC_UPSTREAM}/sph/cubehash.c"
    "${PHASEC_UPSTREAM}/sph/skein.c"
    "${PHASEC_UPSTREAM}/sph/bmw.c"
    "${PHASEC_UPSTREAM}/lyra2/Lyra2.c"
    "${PHASEC_UPSTREAM}/lyra2/Sponge.c")
  target_include_directories(mona_reference PUBLIC "${PHASEC_BACKEND}/include" PRIVATE "${PHASEC_UPSTREAM}")
  target_compile_definitions(mona_reference PRIVATE _CRT_SECURE_NO_WARNINGS)

  # Final Stage-5 SM89 candidate: Stable Ballot + RowOut; NextCol OFF; IMAD OFF.
  # Runtime profile is fixed separately in miner_worker.cpp / GPU integration test.
  add_library(mona_core STATIC
    "${PHASEC_BACKEND}/src/pipeline.cu"
    "${PHASEC_BACKEND}/src/miner.cpp"
    "${PHASEC_BACKEND}/src/build_identity.cpp")
  target_include_directories(mona_core PUBLIC "${PHASEC_BACKEND}/include" PRIVATE
    "${PHASEC_BACKEND}/src"
    "${MONA_GENERATED}"
    "${PHASEC_UPSTREAM}")
  target_link_libraries(mona_core PUBLIC mona_reference CUDA::cudart)
  target_compile_definitions(mona_core PRIVATE
    MONA_TARGET_SM=89
    MONA_LYRA_STABLE_BALLOT=1
    MONA_LYRA_PREFETCH_ROWOUT=1
    MONA_CUBE_IMAD_PHASE_MASK=0)
  set_target_properties(mona_core PROPERTIES
    CUDA_ARCHITECTURES "89-real"
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED YES
    CUDA_SEPARABLE_COMPILATION OFF)
  target_compile_options(mona_core PRIVATE
    "$<$<COMPILE_LANGUAGE:CUDA>:-O3;-lineinfo;-Xptxas=-v,-warn-spills;-Xcompiler=/EHsc>")

  file(GLOB JANSSON_SOURCES CONFIGURE_DEPENDS "${PHASEC_UPSTREAM}/compat/jansson/*.c")
  add_library(phasec_jansson STATIC ${JANSSON_SOURCES})
  target_include_directories(phasec_jansson PUBLIC "${PHASEC_UPSTREAM}/compat/jansson")
  target_compile_definitions(phasec_jansson PRIVATE _CRT_SECURE_NO_WARNINGS HAVE_CONFIG_H)

  add_executable(mona_bench_sm89 "${PHASEC_BACKEND}/src/bench.cpp")
  target_link_libraries(mona_bench_sm89 PRIVATE mona_core)
  set_target_properties(mona_bench_sm89 PROPERTIES OUTPUT_NAME "mona_bench_sm89")

  add_executable(mona_miner
    "${PHASEC_HOST}/src/main.cpp"
    "${PHASEC_HOST}/src/miner_worker.cpp"
    "${PHASEC_HOST}/src/stratum_client_win.cpp")
  target_include_directories(mona_miner PRIVATE "${PHASEC_HOST}/include")
  target_compile_definitions(mona_miner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(mona_miner PRIVATE phasec_core mona_core phasec_jansson Ws2_32)
  set_target_properties(mona_miner PROPERTIES OUTPUT_NAME "mona-miner")

  if(PHASEC_BUILD_TESTS)
    add_executable(phasec_gpu_integration_tests "${CMAKE_CURRENT_SOURCE_DIR}/../tests/phasec_gpu_integration_tests.cpp")
    target_link_libraries(phasec_gpu_integration_tests PRIVATE mona_core)
    add_test(NAME phasec_gpu_integration_tests COMMAND phasec_gpu_integration_tests 0)
  endif()
endif()
