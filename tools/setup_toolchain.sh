#!/usr/bin/env bash
# Local (no-sudo) toolchain + pico-sdk setup for Pico 2 W firmware builds.
set -euo pipefail

TC=/home/subcon/opt
mkdir -p "$TC"
cd "$TC"

log(){ echo "[setup $(date +%H:%M:%S)] $*"; }

# --- ARM GNU bare-metal toolchain (xpack build: reliable for scripted download) ---
if [ ! -d "$TC/arm-none-eabi" ]; then
  log "downloading ARM none-eabi GCC (xpack 13.3.1)"
  curl -fL -o arm.tar.gz \
    https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v13.3.1-1.1/xpack-arm-none-eabi-gcc-13.3.1-1.1-linux-x64.tar.gz
  tar xf arm.tar.gz
  mv xpack-arm-none-eabi-gcc-13.3.1-1.1 arm-none-eabi
  rm arm.tar.gz
  log "ARM GCC: $($TC/arm-none-eabi/bin/arm-none-eabi-gcc --version | head -1)"
fi

# --- CMake (official static linux build) ---
if [ ! -d "$TC/cmake" ]; then
  log "downloading CMake 3.31.6"
  curl -fL -o cmake.tar.gz \
    https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz
  tar xf cmake.tar.gz
  mv cmake-3.31.6-linux-x86_64 cmake
  rm cmake.tar.gz
  log "CMake: $($TC/cmake/bin/cmake --version | head -1)"
fi

# --- pico-sdk 2.1.1 + submodules needed for Pico 2 W + BT + USB ---
if [ ! -d "$TC/pico-sdk" ]; then
  log "cloning pico-sdk 2.1.1"
  git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk "$TC/pico-sdk"
  cd "$TC/pico-sdk"
  log "fetching submodules (tinyusb, btstack, cyw43-driver, lwip, mbedtls)"
  git submodule update --init --depth 1 lib/tinyusb lib/btstack lib/cyw43-driver lib/lwip lib/mbedtls
  cd "$TC"
fi

# --- env file ---
cat > "$TC/pico-env.sh" <<EOF
export PICO_SDK_PATH=$TC/pico-sdk
export PICO_TOOLCHAIN_PATH=$TC/arm-none-eabi
export PATH=$TC/cmake/bin:$TC/arm-none-eabi/bin:\$PATH
EOF

log "DONE. source $TC/pico-env.sh before building."
touch "$TC/.setup_complete"
