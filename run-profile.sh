#!/bin/bash
set -euo pipefail

# Run as the saengra-profiler user on the Hetzner machine.
# Usage: ./run-profile.sh [git-ref]
# Example: ./run-profile.sh main
# Example: ./run-profile.sh feature/optimize-matcher

GIT_REF="${1:-main}"
WORKDIR="$HOME/profile-run"
RESULTS="$HOME/results"

mkdir -p "$WORKDIR" "$RESULTS"

echo "=== Setting up saengra ($GIT_REF) ==="
if [ -d "$WORKDIR/saengra" ]; then
    cd "$WORKDIR/saengra"
    git fetch origin
    git checkout "$GIT_REF"
    git pull origin "$GIT_REF" || true
    git submodule update --init --recursive
else
    git clone --recursive https://github.com/Saluev/saengra.git "$WORKDIR/saengra"
    cd "$WORKDIR/saengra"
    git checkout "$GIT_REF"
fi

echo "=== Setting up venv ==="
if [ ! -d "$WORKDIR/venv" ]; then
    python3 -m venv "$WORKDIR/venv"
fi
source "$WORKDIR/venv/bin/activate"

echo "=== Building saengra (RelWithDebInfo) ==="
AVOID_HOMEBREW=1 SAENGRA_DEBUG=1 pip install --no-cache-dir --force-reinstall -e ".[dev]"
python -c "from saengra.c_extension import DirectAdapter; print('c_extension OK')"

echo "=== Setting up vic ==="
if [ -d "$WORKDIR/vic" ]; then
    cd "$WORKDIR/vic"
    git pull || true
else
    git clone https://github.com/Saluev/vic.git "$WORKDIR/vic"
fi
pip install -r "$WORKDIR/vic/requirements.txt"

echo "=== Running perf ==="
cd "$WORKDIR/saengra"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
PERF_DATA="$RESULTS/perf-${GIT_REF//\//-}-$TIMESTAMP.data"
REPORT="$RESULTS/report-${GIT_REF//\//-}-$TIMESTAMP.txt"

perf record -g --call-graph dwarf -o "$PERF_DATA" -- \
    python -m pytest "$WORKDIR/vic/tests/vic/actions" -v -s 2>&1 | tee "$RESULTS/pytest-output.txt"

perf report -n --stdio --sort=sym,srcline -g none -i "$PERF_DATA" > "$REPORT" 2>/dev/null

echo ""
echo "=== Top 60 functions ==="
head -80 "$REPORT"
echo ""
echo "=== Results saved ==="
echo "  Report: $REPORT"
echo "  Perf data: $PERF_DATA"
echo ""
echo "To view full report: less $REPORT"
echo "To generate flamegraph:"
echo "  perf script -i $PERF_DATA | /tmp/FlameGraph/stackcollapse-perf.pl | /tmp/FlameGraph/flamegraph.pl > $RESULTS/flamegraph-$TIMESTAMP.svg"
