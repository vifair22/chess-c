#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-unix"
CONFIG="${CONFIG:-Release}"

if command -v ninja >/dev/null 2>&1; then
    GENERATOR="-G Ninja"
else
    GENERATOR=""
fi

TOOLCHAIN_FLAG=""
if [ -n "${VCPKG_ROOT:-}" ]; then
    TOOLCHAIN_FLAG="-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
fi

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" ${GENERATOR} -DCMAKE_BUILD_TYPE="${CONFIG}" ${TOOLCHAIN_FLAG}
cmake --build "${BUILD_DIR}" --parallel
