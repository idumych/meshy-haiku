#!/bin/sh
# Copyright 2026, the Meshy for Haiku contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Differential test: runs the same deterministic protocol cases through this
# C++ port and through upstream Meshy's Python, then compares them.
#
# Usage: tests/run_diff.sh /path/to/meshy-upstream
#
# The upstream checkout needs no dependencies installed — the modules these
# cases exercise are pure standard library.

set -e

UPSTREAM="$1"
if [ -z "$UPSTREAM" ]; then
    echo "usage: $0 /path/to/meshy-upstream" >&2
    exit 2
fi
if [ ! -f "$UPSTREAM/src/protocol.py" ]; then
    echo "error: $UPSTREAM does not look like a Meshy checkout" >&2
    exit 2
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build

CXX=${CXX:-g++}
echo "building the C++ generator…"
$CXX -std=c++17 -Wall -Wextra -Isrc/core \
    tests/diffgen.cpp \
    src/core/Bytes.cpp src/core/Crypto.cpp src/core/MeshPacket.cpp \
    src/core/Protocol.cpp src/core/Smaz.cpp \
    -lcrypto -o build/diffgen

echo "running both generators…"
./build/diffgen > build/cpp.txt
MESHY_UPSTREAM="$UPSTREAM" python3 -I tests/diffgen.py > build/py.txt

if diff -u build/py.txt build/cpp.txt > build/diff.txt; then
    echo "PASS — $(wc -l < build/cpp.txt) cases identical to upstream"
    exit 0
fi

echo "FAIL — differences (upstream is -, this port is +):"
head -60 build/diff.txt
echo "full diff in build/diff.txt"
exit 1
