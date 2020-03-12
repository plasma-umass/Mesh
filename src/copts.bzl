# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

COMMON_FLAGS = [
    # TODO: have config option to disable this
    "-march=westmere",
    "-mavx",

    # warn on lots of stuff; this is cargo-culted from the old Make build system
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
]

MESH_LLVM_FLAGS = []

MESH_GCC_FLAGS = [
    "-Wa,--noexecstack",
]

MESH_DEFAULT_COPTS = select({
    "//src:llvm": COMMON_FLAGS + MESH_LLVM_FLAGS,
    "//conditions:default": COMMON_FLAGS + MESH_GCC_FLAGS,
})
