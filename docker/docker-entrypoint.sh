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
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++

echo "=== Building with Ninja + GCC | RelWithDebInfo | All ==="
cmake --build . --config RelWithDebInfo -j"$(nproc)"
cd ../

mkdir -p build/addons/metamod
mkdir -p build/addons/AcceleratorCSS/bin/linuxsteamrt64
mkdir -p build/addons/counterstrikesharp/plugins

dotnet publish managed/AcceleratorCSS_CSS/AcceleratorCSS_CSS.csproj -c Release -o build/addons/counterstrikesharp/plugins/AcceleratorCSS_CSS \
  -p:SEMVER="$SEMVER" \
  -p:GITHUB_SHA_SHORT="$GITHUB_SHA_SHORT"
