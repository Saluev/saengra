#!/bin/bash
set -euo pipefail

# Run this as root on the Hetzner machine.
# Usage: sudo ./setup-profiler.sh

USERNAME=saengra-profiler

echo "=== Creating user ==="
useradd -m -s /bin/bash "$USERNAME" || echo "User already exists"

echo "=== Installing system dependencies ==="
apt-get update
apt-get install -y \
    build-essential cmake git \
    libprotobuf-dev protobuf-compiler libboost-dev libspdlog-dev \
    bison flex linux-tools-common linux-tools-$(uname -r) \
    python3-pip python3-venv

echo "=== Building abseil from source ==="
if [ ! -f /usr/local/lib/cmake/absl/abslConfig.cmake ]; then
    git clone --depth 1 --branch 20240116.2 https://github.com/abseil/abseil-cpp.git /tmp/abseil
    cd /tmp/abseil
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DABSL_ENABLE_INSTALL=ON \
        -DABSL_USE_SYSTEM_INCLUDES=ON
    cmake --build build -j$(nproc)
    cmake --install build
    ldconfig
    rm -rf /tmp/abseil
fi

echo "=== Allowing perf for all users ==="
sysctl -w kernel.perf_event_paranoid=-1
echo "kernel.perf_event_paranoid = -1" > /etc/sysctl.d/99-perf.conf

echo "=== Done ==="
echo "Now run as $USERNAME:"
echo "  sudo -u $USERNAME -i bash /path/to/run-profile.sh"
