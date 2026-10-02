cmake_minimum_required(VERSION 3.26)
project(mona_miner LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Mona Miner requires a 64-bit build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PHASEC_BUILD_MINER "Build the Windows CUDA miner" ${WIN32})
option(PHASEC_BUILD_TESTS "Build portable host-side tests" ON)

set(PHASEC_UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/pinned-upstream")
set(PHASEC_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/certified-backend")
set(PHASEC_HOST "${CMAKE_CURRENT_SOURCE_DIR}/phasec-host")

# Portable work/target/session core. SHA256 is taken from the pinned upstream snapshot.
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
    message(FATAL_ERROR "Mona Miner acceptance build requires CUDA 13.4.x")
  endif()
  find_package(CUDAToolkit REQUIRED)

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

  # The CUDA/backend sources and generated headers are the sealed Phase B snapshot.
  # The compile definitions below reproduce the approved stable profile exactly:
  # stable ballot ON, rowOut prefetch ON, nextCol OFF, cube IMAD mask 15.
  add_library(mona_core STATIC
    "${PHASEC_BACKEND}/src/pipeline.cu"
    "${PHASEC_BACKEND}/src/miner.cpp"
    "${PHASEC_BACKEND}/src/build_identity.cpp")
  target_include_directories(mona_core PUBLIC "${PHASEC_BACKEND}/include" PRIVATE
    "${PHASEC_BACKEND}/src"
    "${PHASEC_BACKEND}/generated"
    "${PHASEC_UPSTREAM}")
  target_link_libraries(mona_core PUBLIC mona_reference CUDA::cudart)
  target_compile_definitions(mona_core PRIVATE
    MONA_TARGET_SM=120
    MONA_LYRA_STABLE_BALLOT=1
    MONA_LYRA_PREFETCH_ROWOUT=1
    MONA_CUBE_IMAD_PHASE_MASK=15)
  set_target_properties(mona_core PROPERTIES
    CUDA_ARCHITECTURES "120-real"
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED YES
    CUDA_SEPARABLE_COMPILATION OFF)
  target_compile_options(mona_core PRIVATE
    "$<$<COMPILE_LANGUAGE:CUDA>:-O3;-lineinfo;-Xptxas=-v,-warn-spills;-Xcompiler=/EHsc>")
  # Approved profile has MONA_MAX_REGISTERS=0: intentionally no --maxrregcount.

  file(GLOB JANSSON_SOURCES CONFIGURE_DEPENDS "${PHASEC_UPSTREAM}/compat/jansson/*.c")
  add_library(phasec_jansson STATIC ${JANSSON_SOURCES})
  target_include_directories(phasec_jansson PUBLIC "${PHASEC_UPSTREAM}/compat/jansson")
  target_compile_definitions(phasec_jansson PRIVATE _CRT_SECURE_NO_WARNINGS HAVE_CONFIG_H)

  # Qualification bench links the exact same mona_core static library as the pool miner.
  # It exists so the newly-built Phase C CUDA object can be requalified; the old Phase B
  # embedded build_id is source-authority metadata, not certification of this new binary.
  add_executable(mona_bench_sm120 "${PHASEC_BACKEND}/src/bench.cpp")
  target_link_libraries(mona_bench_sm120 PRIVATE mona_core)
  set_target_properties(mona_bench_sm120 PROPERTIES OUTPUT_NAME "mona_bench_sm120")

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
