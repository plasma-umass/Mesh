# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

COMMON_FLAGS = [
    "-fPIC",
    "-pipe",
    "-fno-omit-frame-pointer",
    "-ffunction-sections",
    "-fdata-sections",
    "-fvisibility=hidden",
    "-Wall",
    "-Wextra",
    "-Werror=pointer-arith",
    "-pedantic",
    "-Wno-unused-parameter",
    "-Wno-unused-variable",
    "-Woverloaded-virtual",
    "-Werror=return-type",
    "-Wtype-limits",
    "-Wempty-body",
    "-Winvalid-offsetof",
    "-Wvariadic-macros",
    "-Wcast-align",
    "-pthread",
]

ARCH_FLAGS = select({
    "@platforms//cpu:x86_64": [
        "-march=westmere",
        "-mavx2",
    ],
    "@platforms//cpu:aarch64": [
        "-march=armv8.2-a",
    ],
    "//conditions:default": [],
})

LINUX_FLAGS = select({
    "@platforms//os:linux": [
        "-Wa,--noexecstack",
    ],
    "//conditions:default": [],
})

LTO_FLAGS = select({
    "//src:opt_build": [
        "-U_FORTIFY_SOURCE",
        "-D_FORTIFY_SOURCE=2",
        "-flto",
    ],
    "//conditions:default": [],
})

# On Linux, use gold linker instead of lld for GCC LTO compatibility.
# Bazel may auto-detect lld and configure it, but GCC's LTO bytecode
# is incompatible with lld (which only supports LLVM's LTO format).
# We use gold rather than bfd because gold supports --start-lib/--end-lib.
LINKER_FLAGS = select({
    "@platforms//os:linux": ["-fuse-ld=gold"],
    "//conditions:default": [],
})

LTO_LINKOPTS = select({
    "//src:opt_build": ["-flto"],
    "//conditions:default": [],
})

MESH_DEFAULT_COPTS = COMMON_FLAGS + ARCH_FLAGS + LINUX_FLAGS + LTO_FLAGS
MESH_DEFAULT_LINKOPTS = LINKER_FLAGS + LTO_LINKOPTS
