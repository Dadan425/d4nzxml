#!/bin/bash
# ================================================================
#  Build Script — PS4 Syscon Reader untuk Raspberry Pi Pico
#  Jalankan di WSL (Windows) atau Linux
# ================================================================

set -e

echo "============================================"
echo "  JGC Pico Syscon Reader — Build Script"
echo "============================================"

# ── Step 1: Install dependencies ─────────────────────────────
echo ""
echo "[1/5] Install tools..."
sudo apt-get update -qq
sudo apt-get install -y \
    cmake \
    ninja-build \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3

# ── Step 2: Clone Pico SDK ────────────────────────────────────
echo ""
echo "[2/5] Setup Pico SDK..."
if [ ! -d "$HOME/pico-sdk" ]; then
    echo "  Cloning Pico SDK..."
    git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git "$HOME/pico-sdk"
    cd "$HOME/pico-sdk"
    git submodule update --init --depth 1
    cd -
else
    echo "  Pico SDK sudah ada di $HOME/pico-sdk"
fi

export PICO_SDK_PATH="$HOME/pico-sdk"
echo "  PICO_SDK_PATH=$PICO_SDK_PATH"

# ── Step 3: Configure ────────────────────────────────────────
echo ""
echo "[3/5] CMake configure..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="$PICO_SDK_PATH"

# ── Step 4: Build ─────────────────────────────────────────────
echo ""
echo "[4/5] Compiling..."
ninja -j$(nproc)

# ── Step 5: Done ──────────────────────────────────────────────
echo ""
echo "[5/5] Selesai!"
echo ""
echo "  File UF2 ada di:"
echo "  $BUILD_DIR/JGC_Syscon_Reader.uf2"
echo ""
echo "  Cara flash ke Pico:"
echo "  1. Tahan tombol BOOTSEL di Pico"
echo "  2. Colok USB ke PC"
echo "  3. Lepas BOOTSEL — Pico muncul sebagai drive RPI-RP2"
echo "  4. Copy JGC_Syscon_Reader.uf2 ke drive itu"
echo "  5. Pico restart otomatis — muncul sebagai COM port"
echo ""

# Copy UF2 ke folder project juga buat gampang
cp "$BUILD_DIR/JGC_Syscon_Reader.uf2" "$SCRIPT_DIR/" 2>/dev/null && \
    echo "  UF2 juga dicopy ke: $SCRIPT_DIR/JGC_Syscon_Reader.uf2"
