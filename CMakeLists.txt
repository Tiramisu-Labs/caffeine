# Set the minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_COMPILER gcc)
# Define the project name and language
project(caffeine C)

# ===============================================
# Wasmtime Integration Variables
# ===============================================

# 1. Define the base directory for Wasmtime (adjust this path as necessary)
set(WASM_BASE_DIR ${CMAKE_SOURCE_DIR}/deps/wasmtime)

# 2. List the required Wasmtime libraries.
# NOTE: This list is crucial and MUST include all dependencies Wasmtime requires 
# (e.g., cranelift, dl, pthread, etc.). You must confirm the exact list for your build.
set(WASM_LIBS
    wasmtime
    # Common dependencies for Wasmtime compiled from source/static builds:
    dl
    pthread
    m
)

# ===============================================
# Target Definition
# ===============================================

# Specify where to look for global header files
include_directories(include)

# Create an executable named 'caffeine' from the source files
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

# ===============================================
# Wasmtime Linking
# ===============================================

# Tell the compiler where to find the Wasmtime header files (.h)
target_include_directories(caffeine
    PUBLIC
        ${WASM_BASE_DIR}/include
)

# Tell the linker where the static library (.a) file is located
target_link_directories(caffeine
    PUBLIC
        ${WASM_BASE_DIR}/lib
)

# Link the executable against the necessary Wasmtime libraries
target_link_libraries(caffeine
    PUBLIC
        ${WASM_LIBS}
)

# ===============================================
# Installation Rule
# ===============================================

# Define the installation rule for the executable
install(TARGETS caffeine DESTINATION bin)