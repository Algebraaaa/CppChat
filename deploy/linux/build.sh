#!/bin/bash
set -euo pipefail
cd /opt/cppchat/source
cmake -S . -B /opt/cppchat/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O1 -DNDEBUG'
cmake --build /opt/cppchat/build --parallel 1
printf '\nCPPCHAT_BUILD_READY\n'
