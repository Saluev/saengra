# syntax=docker/dockerfile:1.7
# Official TTC 2018 Differential Datalog (DDlog) solution, with the generated Rust code built
# natively for the host architecture.
# Build context: the `solutions` directory of https://github.com/TransformationToolContest/ttc2018liveContest
# Run: /opt/snq1 HARNESS (Q1) or /opt/snq2 HARNESS (Q2), with the usual harness env vars.
#
# Deviations from the official build (solution.ini / build.sh), none of which touch the solution's code:
# - The DDlog v1.2.3 compiler is an x86_64-only Haskell binary, so only the `ddlog -i snqN.dl`
#   step runs under linux/amd64 (emulated on arm64 hosts). It only generates Rust sources.
# - Rust is pinned to nightly-2022-05-18, the toolchain of the official differential image
#   (DDlog v1.2.3 requires rustc >= 1.52.1).
# - Neither the solution nor the generated crates ship a Cargo.lock, and today's crates.io
#   versions don't target that era. So the lockfiles are resolved as of the DDlog v1.2.3 release
#   date (2021-12-14, cargo's unstable `generate-lockfile --publish-time`). Many crate versions
#   of that time (e.g. all futures-util 0.3.0-0.3.30, crossbeam-utils 0.8.0-0.8.6) have since
#   been yanked, which that resolver refuses, so it's pointed at a tiny local proxy of the
#   crates.io sparse index that clears the `yanked` flags. timely / differential-dataflow come
#   from DDlog's own git forks (branch ddlog-4, last commits 2021), exactly as generated.
# - build.sh also builds the snqN_ddlog crate standalone (its CLI and static library); only the
#   snqN_lib binaries the run commands use are built here (they compile snqN_ddlog as a library).

FROM --platform=linux/amd64 debian:bookworm AS generate
RUN apt-get update && apt-get install -y --no-install-recommends wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY differential-datalog /build
RUN cd /build && ddlog/download.sh \
    && export DDLOG_HOME=/build/ddlog \
    && (cd snq1 && $DDLOG_HOME/bin/ddlog -i snq1.dl) \
    && (cd snq2 && $DDLOG_HOME/bin/ddlog -i snq2.dl)

FROM debian:bookworm AS lock
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates git python3 \
    && rm -rf /var/lib/apt/lists/*
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain nightly-2026-10-19
ENV PATH=/root/.cargo/bin:$PATH
COPY <<'EOF' /unyank.py
# crates.io sparse index proxy that marks every version as not yanked
import http.server, urllib.request, urllib.error
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/config.json":
            body = b'{"dl":"https://static.crates.io/crates","api":"https://crates.io"}'
        else:
            try:
                with urllib.request.urlopen("https://index.crates.io" + self.path) as r:
                    body = r.read().replace(b'"yanked":true', b'"yanked":false')
            except urllib.error.HTTPError as e:
                self.send_response(e.code); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(("127.0.0.1", 8000), H).serve_forever()
EOF
COPY <<'EOF' /root/.cargo/config.toml
[source.crates-io]
replace-with = "unyanked"
[source.unyanked]
registry = "sparse+http://127.0.0.1:8000/"
EOF
COPY --from=generate /build /build
RUN (python3 /unyank.py &) && sleep 1 \
    && for q in snq1 snq2; do \
         (cd /build/$q/${q}_lib \
          && cargo generate-lockfile -Zunstable-options --publish-time 2021-12-14T00:00:00Z \
          && sed -i 's/^version = 4$/version = 3/' Cargo.lock) || exit 1; \
       done

FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates gcc libc6-dev git \
    && rm -rf /var/lib/apt/lists/*
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain nightly-2022-05-18
ENV PATH=/root/.cargo/bin:$PATH CARGO_NET_GIT_FETCH_WITH_CLI=true CARGO_TARGET_DIR=/build/target
COPY --from=generate /build /build
COPY --from=lock /build/snq1/snq1_lib/Cargo.lock /build/snq1/snq1_lib/Cargo.lock
COPY --from=lock /build/snq2/snq2_lib/Cargo.lock /build/snq2/snq2_lib/Cargo.lock
RUN cd /build/snq1/snq1_lib && cargo build --release --locked -q
RUN cd /build/snq2/snq2_lib && cargo build --release --locked -q

FROM debian:bookworm-slim
COPY --from=build /build/target/release/snq1_lib /opt/snq1
COPY --from=build /build/target/release/snq2_lib /opt/snq2
WORKDIR /tmp
