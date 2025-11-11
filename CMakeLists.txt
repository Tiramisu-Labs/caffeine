# Set the minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_COMPILER gcc)

project(caffeine C)

set(WASM_BASE_DIR ${CMAKE_SOURCE_DIR}/deps/wasmtime)

set(WASM_LIBS
    wasmtime
    dl
    pthread
    m
)

include_directories(include)

add_executable(
    caffeine
    src/caffeine.c
    src/worker.c
    src/daemon.c
    src/log.c
    src/caffeine_cfg.c
    src/caffeine_sig.c
    src/caffeine_utils.c
    src/list_instances.c
    src/headers.c
    src/deploy.c
)

target_link_directories(caffeine
    PUBLIC
        ${WASM_BASE_DIR}/lib
)

set_target_properties(caffeine PROPERTIES
    BUILD_RPATH "${WASM_BASE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/../lib"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

target_include_directories(caffeine
    PUBLIC
        ${WASM_BASE_DIR}/include
)

target_link_libraries(caffeine
    PUBLIC
        ${WASM_LIBS}
)

install(FILES 
    "${WASM_BASE_DIR}/lib/libwasmtime.so"
    DESTINATION lib
)

install(TARGETS caffeine DESTINATION bin)

install(TARGETS caffeine DESTINATION bin)