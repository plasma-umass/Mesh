#!/usr/bin/env bash
set -euo pipefail
# set -x

for so in bazel-bin/src/libmesh.so libmesh.so; do
  echo "LIB $so"
  ldd $so | sort
  ls -laht $so
done
