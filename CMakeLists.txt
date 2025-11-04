# Set the minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

# Define the project name and language
project(caffeine C)

# Specify where to look for header files
# This adds 'include/' to the search path for headers
include_directories(include)

# Create an executable named 'caffeine' from the source files
# The executable will be built from caffeine.c and worker.c
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
    )

# Define the installation rule for the executable
# It will install the 'caffeine' executable to the 'bin' directory
# of the specified install prefix.
install(TARGETS caffeine DESTINATION bin)
