#!/usr/bin/env bash
set -euo pipefail
configuration="${1:-Debug}"
build="${2:-build/linux}"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE="$configuration" -DCPPCHAT_BUILD_TESTS=ON "${@:3}"
cmake --build "$build" --parallel 2
cmake -S CppChat -B build/qt-linux -G Ninja -DCMAKE_BUILD_TYPE="$configuration" -DCPPCHAT_BUILD_TESTS=ON -DCPPCHAT_DEPLOY_QT=OFF
cmake --build build/qt-linux --parallel 2
