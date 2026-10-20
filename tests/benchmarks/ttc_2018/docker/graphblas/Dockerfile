# Official TTC 2018 GraphBLAS solution ("GB-Incr-x1" = incremental, single-threaded),
# built natively for the host architecture.
# Build context: the `solutions` directory of https://github.com/TransformationToolContest/ttc2018liveContest
#
# Dependencies as pinned in GraphBLAS/README.md: SuiteSparse:GraphBLAS v4.0.3 and LAGraph at
# commit 99ad411. Ubuntu 20.04 (gcc 9) is used as the toolchain contemporary with those versions.
# Single-threaded: ThreadsNum=1 makes the solution set GxB_NTHREADS=1; OMP_NUM_THREADS=1 is
# also set so that nothing else in the process spins up an OpenMP pool.
FROM ubuntu:20.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake curl ca-certificates m4 \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir /deps && cd /deps \
    && curl -sSfL https://github.com/DrTimothyAldenDavis/GraphBLAS/archive/refs/tags/v4.0.3.tar.gz | tar zx \
    && cd GraphBLAS-4.0.3 && make JOBS=$(nproc) && make install && ldconfig
RUN cd /deps \
    && curl -sSfL https://github.com/GraphBLAS/LAGraph/archive/99ad4114cd6b3116195e9599fb9709780e06fc9d.tar.gz | tar zx \
    && cd LAGraph-* && make JOBS=$(nproc) && make install && ldconfig
COPY GraphBLAS /build/ttc
RUN cd /build/ttc && mkdir -p cmake-build-benchmark && cd cmake-build-benchmark \
    && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)

FROM ubuntu:20.04
RUN apt-get update && apt-get install -y --no-install-recommends libgomp1 \
    && rm -rf /var/lib/apt/lists/*
# Only the sonames the binary links against (a glob would duplicate the ~100 MB GraphBLAS lib per symlink).
COPY --from=build /usr/local/lib/libgraphblas.so.4 /usr/local/lib/liblagraph.so.0 /usr/local/lib/
RUN ldconfig
COPY --from=build /build/ttc/cmake-build-benchmark/ttc /opt/ttc
ENV ThreadsNum=1 OMP_NUM_THREADS=1
