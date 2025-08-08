#!/bin/bash

set -e

BUILD_DIR="build"

# Define the installation prefix, which can be changed by the user
# Default to ~/.local, or use the first command-line argument if provided
INSTALL_PREFIX=${1:-$HOME/.local}

# Create the build directory if it doesn't exist
mkdir -p "$BUILD_DIR"

# Navigate into the build directory
cd "$BUILD_DIR"

# Run CMake with the specified installation prefix
cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

# Build the project
make

echo "Build complete. To install, run 'make install' in the '$BUILD_DIR' directory."
echo "The program will be installed to: $INSTALL_PREFIX/bin"