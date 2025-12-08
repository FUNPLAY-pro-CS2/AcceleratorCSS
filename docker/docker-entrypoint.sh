#!/bin/bash
set -e

git submodule update --init --recursive

if git describe --tags --exact-match >/dev/null 2>&1; then
  export SEMVER="$(git describe --tags --exact-match)"
fi

export GITHUB_SHA_SHORT="$(git rev-parse --short HEAD)"

SDK_DIR="/tmp/sdk"
HL2SDK_DIR="$SDK_DIR/hl2sdk-cs2"
MMSOURCE_DIR="$SDK_DIR/metamod-source"
CSGO_PROTO_DIR="$SDK_DIR/Protobufs"

echo "=== Preparing temporary SDK directory ==="
rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR"

echo "=== Downloading HL2SDK-CS2 ==="
git clone --depth=1 -b cs2 https://github.com/alliedmodders/hl2sdk "$HL2SDK_DIR"

echo "=== Downloading Metamod-Source ==="
git clone --depth=1 https://github.com/alliedmodders/metamod-source "$MMSOURCE_DIR"

echo "=== Downloading Protobufs ==="
git clone --depth=1 https://github.com/SteamDatabase/Protobufs "$CSGO_PROTO_DIR"

### --- Export env vars for CMake ------------------------------------------
export HL2SDKCS2="$HL2SDK_DIR"
export MMSOURCE_DEV="$MMSOURCE_DIR"
export CSGO_PROTO="$CSGO_PROTO_DIR/csgo"

echo "Using HL2SDKCS2=$HL2SDKCS2"
echo "Using MMSOURCE_DEV=$MMSOURCE_DEV"
echo "Using CSGO_PROTO=$CSGO_PROTO"

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
mkdir -p build/addons/AcceleratorCSS/logs
mkdir -p build/addons/counterstrikesharp/plugins
mkdir -p build/addons/counterstrikesharp/shared/0Harmony

cp managed/0Harmony.dll \
   build/addons/counterstrikesharp/shared/0Harmony/0Harmony.dll

dotnet publish managed/AcceleratorCSS_CSS/AcceleratorCSS_CSS.csproj -c Release -o build/addons/counterstrikesharp/plugins/AcceleratorCSS_CSS \
  -p:SEMVER="$SEMVER" \
  -p:GITHUB_SHA_SHORT="$GITHUB_SHA_SHORT"
