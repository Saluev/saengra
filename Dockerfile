ARG PYTHON_VERSION=3.12
FROM python:${PYTHON_VERSION}-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libprotobuf-dev \
    protobuf-compiler \
    libboost-dev \
    libspdlog-dev \
    bison \
    flex \
    && rm -rf /var/lib/apt/lists/*

# Build and install abseil from source
RUN git clone --depth 1 --branch 20240116.2 https://github.com/abseil/abseil-cpp.git /tmp/abseil \
    && cd /tmp/abseil \
    && cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DABSL_ENABLE_INSTALL=ON \
        -DABSL_USE_SYSTEM_INCLUDES=ON \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && rm -rf /tmp/abseil

WORKDIR /app
COPY . .

ENV AVOID_HOMEBREW=1
RUN pip install --no-cache-dir ".[dev]" && pip uninstall -y saengra && pip install --no-cache-dir . && rm -rf ./saengra

ENV SKIP_BENCHMARKS=1
CMD ["python", "-m", "pytest", "tests/", "-v"]
