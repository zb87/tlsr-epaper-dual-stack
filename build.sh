#!/usr/bin/env bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

echo "Building TLSR8258 E-Paper Dual-Stack firmware..."
make clean
make -j$(nproc)

echo "Build complete. Artifacts in bin/:"
ls -lh bin/
