#!/usr/bin/env bash
# Build Redis and Valkey twice each, for bench/kv/fragmentation.py:
#   <name>-server-jemalloc  default build (bundled jemalloc; supports activedefrag)
#   <name>-server-libc      MALLOC=libc, so LD_PRELOAD=libmesh.so can interpose
# plus <name>-cli.
#
# Usage: bench/kv/build-servers.sh [OUT_DIR]   (default: bench/kv/build)
# Env:   REDIS_VERSION, VALKEY_VERSION, KV_SRC_DIR (pre-downloaded tarballs:
#        $KV_SRC_DIR/redis-$REDIS_VERSION.tar.gz, valkey-$VALKEY_VERSION.tar.gz)
set -euo pipefail

REDIS_VERSION=${REDIS_VERSION:-8.0.2}
VALKEY_VERSION=${VALKEY_VERSION:-9.1.0}
OUT=$(realpath -m "${1:-$(dirname "$0")/build}")
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
mkdir -p "$OUT/src" "$OUT/bin"

fetch() {  # name version url
  local tgz="$OUT/src/$1-$2.tar.gz"
  if [ ! -f "$tgz" ]; then
    if [ -n "${KV_SRC_DIR:-}" ] && [ -f "$KV_SRC_DIR/$1-$2.tar.gz" ]; then
      cp "$KV_SRC_DIR/$1-$2.tar.gz" "$tgz"
    else
      curl -fsSL -o "$tgz" "$3"
    fi
  fi
}

build() {  # name version url
  local name=$1 version=$2
  for malloc in jemalloc libc; do
    if [ -x "$OUT/bin/$name-server-$malloc" ]; then
      continue
    fi
    fetch "$name" "$version" "$3"
    local dir="$OUT/src/$name-$version-$malloc"
    rm -rf "$dir" && mkdir -p "$dir"
    tar -xzf "$OUT/src/$name-$version.tar.gz" -C "$dir" --strip-components=1
    echo "building $name $version (MALLOC=$malloc)"
    make -C "$dir" -j"$JOBS" MALLOC="$malloc" BUILD_TLS=no > "$dir/build.log" 2>&1 ||
      { tail -40 "$dir/build.log"; exit 1; }
    cp "$dir/src/$name-server" "$OUT/bin/$name-server-$malloc"
    cp "$dir/src/$name-cli" "$OUT/bin/$name-cli"
  done
}

build redis "$REDIS_VERSION" "https://github.com/redis/redis/archive/refs/tags/$REDIS_VERSION.tar.gz"
build valkey "$VALKEY_VERSION" "https://github.com/valkey-io/valkey/archive/refs/tags/$VALKEY_VERSION.tar.gz"

printf '%s\n' "redis $REDIS_VERSION" "valkey $VALKEY_VERSION" > "$OUT/bin/VERSIONS"
ls -1 "$OUT/bin"
