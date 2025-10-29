#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")"/../.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build-cmake"

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DDESMUME_SDL=ON -DDESMUME_OPENGL=OFF -DDESMUME_OPENGLES=OFF -DDESMUME_OPENAL=OFF -DDESMUME_ALSA=OFF
cmake --build "${BUILD_DIR}" -j"$(getconf _NPROCESSORS_ONLN || echo 4)"

