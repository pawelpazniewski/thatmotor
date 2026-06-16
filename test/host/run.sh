#!/usr/bin/env bash
# Build and run the host-side pure-logic unit tests in one command.
# Requires only cmake + ninja + a host C compiler (no ESP-IDF).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

cmake -GNinja -B build
ninja -C build
./build/host_tests
