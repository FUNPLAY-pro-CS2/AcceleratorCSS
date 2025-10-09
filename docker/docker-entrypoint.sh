#!/bin/bash
set -e

git submodule update --init --recursive

if git describe --tags --exact-match >/dev/null 2>&1; then
  export SEMVER="$(git describe --tags --exact-match)"
fi

export GITHUB_SHA_SHORT="$(git rev-parse --short HEAD)"

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++

echo "=== Building with GCC | RelWithDebInfo | All ==="
cmake --build . --config RelWithDebInfo -j"$(nproc)"

mkdir -p addons/metamod
mkdir -p addons/AcceleratorCSS
mkdir -p addons/AcceleratorCSS/bin/linuxsteamrt64
mkdir -p addons/counterstrikesharp/plugins
mkdir -p addons/counterstrikesharp/shared/0Harmony

cp ../managed/0Harmony.dll \
   addons/counterstrikesharp/shared/0Harmony/0Harmony.dll

dotnet publish ../managed/AcceleratorCSS_CSS/AcceleratorCSS_CSS.csproj -c Release -o addons/counterstrikesharp/plugins/AcceleratorCSS_CSS