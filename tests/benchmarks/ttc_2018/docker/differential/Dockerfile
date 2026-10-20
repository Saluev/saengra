# Official TTC 2018 Differential Dataflow solution, built natively for the host architecture
# (the images published on Docker Hub are amd64-only).
# Build context: the `solutions` directory of https://github.com/TransformationToolContest/ttc2018liveContest
#
# The solution has no Cargo.lock, and with today's toolchain and dependency versions it runs
# away (memory grows by GBs per second, even on the smallest model). So, as in the official
# image, it's built with nightly-2022-05-18, using the oldest dependency versions matching
# Cargo.toml, except for two crates whose oldest versions don't compile with that toolchain.
FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain nightly-2022-05-18
ENV PATH=/root/.cargo/bin:$PATH
COPY differential /build/differential
RUN cd /build/differential \
    && cargo generate-lockfile -Z minimal-versions \
    && cargo update -p getopts --precise 0.2.21 \
    && cargo update -p unicode-width --precise 0.1.9 \
    && cargo build --release --locked -q

FROM debian:bookworm-slim
COPY --from=build /build/differential/target/release/differential /opt/differential
