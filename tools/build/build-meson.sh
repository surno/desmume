#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")"/../.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build-meson"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

meson setup . "${ROOT_DIR}/desmume/src/frontend/api/interface" \
  -Dopengl=false -Dopengles=false -Dopenal=false -Dfrontend-gtk=false -Dfrontend-cli=true
meson compile -C .

