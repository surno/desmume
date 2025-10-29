#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")"/../.. && pwd)"
POSIX_DIR="${ROOT_DIR}/desmume/src/frontend/posix"
BUILD_DIR="${ROOT_DIR}/build-autotools"

mkdir -p "${BUILD_DIR}"
cd "${POSIX_DIR}"

autoreconf -fi
./configure --enable-opengl=no --enable-opengles=no --enable-openal=no
make -j"$(getconf _NPROCESSORS_ONLN || echo 4)"

