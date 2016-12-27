#!/usr/bin/env bash

#abort if any command fails
set -e

builddir=$(readlink -f build)

export LD_LIBRARY_PATH=$builddir/lib

valgrind --leak-check=full src/circ_obfuscator "$@"
