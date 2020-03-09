# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

load("//src:copts.bzl", "MESH_DEFAULT_COPTS")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])  # Apache 2.0

config_setting(
    name = "llvm",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "llvm",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "disable_meshing",
    values = {
        "define": "disable_meshing=true",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "disable_randomization",
    values = {
        "define": "disable_randomization=true",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "shuffle_on_free",
    values = {
        "define": "shuffle_on_free=true",
    },
    visibility = ["//visibility:private"],
)

NO_BUILTIN_MALLOC = [
    "-fno-builtin-malloc",
    "-fno-builtin-free",
]

COMMON_DEFINES = [] + select({
    ":disable_meshing": ["MESHING_ENABLED=0"],
    "//conditions:default": ["MESHING_ENABLED=1"],
}) + select({
    ":disable_randomization": ["SHUFFLE_ON_INIT=0"],
    "//conditions:default": ["SHUFFLE_ON_INIT=1"],
}) + select({
    ":shuffle_on_free": ["SHUFFLE_ON_FREE=1"],
    "//conditions:default": ["SHUFFLE_ON_FREE=0"],
}) + select({
    "@bazel_tools//src/conditions:linux_x86_64": [
        # "_FORTIFY_SOURCE=2",  # TODO: only in release; but I think Bazel takes care of this?
        "_DEFAULT_SOURCE",
        "_BSD_SOURCE",
        "_XOPEN_SOURCE=700",
    ],
    "//conditions:default": [],
})

# common_copts = [
#     # this doesn't seem to work -- both this and -std=c++11 end up on the commandline
#     # TODO: have config option to disable this
#     "-march=westmere",
#     "-mavx",
#     # TODO: should we specify this or should a toolchain opt take care of it?
#     # "-fPIC",
#     # "-ffunction-sections",
#     # "-fdata-sections",
#     #
#     # default to hidden visibility for symbols; we will explicitly export what we want
#     # "-fvisibility=hidden", # TODO: I'm pretty sure toolchain takes care of this
#     # "-Wa,--noexecstack", # TODO: only for gcc
# ] + select({
#     "@bazel_tools//src/conditions:linux_x86_64": [
#         # "-flto",
#     ],
#     "//conditions:default": [],
# })

COMMON_LINKOPTS = [
    # TODO: only in release/opt build
    "-O3",
    "-lm",
    "-lpthread",
    "-ldl",
    "-march=westmere",
    "-mavx",
] + select({
    "@bazel_tools//src/conditions:linux_x86_64": [
        "-Wl,--no-as-needed",
        "-Wl,--no-add-needed",
        "-Wl,--sort-common",
        "-Wl,--hash-style=both",
        "-Wl,--no-undefined",
        "-Wl,-Bsymbolic-functions",
        "-Wl,-z,now,-z,relro",
        "-ftls-model=initial-exec",
        "-Wl,--exclude-libs=libc++.a",
        "-Wl,--exclude-libs=libc++abi.a",
        "-Wl,--exclude-libs=libunwind.a",
    ],
    "//conditions:default": [],
})

cc_library(
    name = "mesh-core",
    srcs = [
        "d_assert.cc",
        "global_heap.cc",
        "measure_rss.cc",
        "meshable_arena.cc",
        "real.cc",
        "runtime.cc",
        "thread_local_heap.cc",
    ],
    hdrs = glob([
        "*.h",
        "plasma/*.h",
        "rng/*.h",
        "gnu_wrapper.cc",
        "static/*.h",
        "size_classes.def",
    ]),
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS,
    linkstatic = True,
    visibility = ["//visibility:private"],
    deps = [
        "@org_heaplayers",
    ],
)

cc_library(
    name = "mesh-lib",
    srcs = [
        "libmesh.cc",
    ],
    hdrs = glob([
        "*.h",
        "plasma/*.h",
        "rng/*.h",
        "gnu_wrapper.cc",
        "static/*.h",
        "size_classes.def",
        "wrapper.cc",
    ]),
    # TODO: config options here too
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS,
    linkstatic = True,
    visibility = ["//visibility:private"],
    deps = [
        "@org_heaplayers",
    ],
)

cc_binary(
    name = "libmesh.so",
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS,
    linkshared = True,
    visibility = ["//visibility:public"],
    deps = [
        ":mesh-core",
        ":mesh-lib",
    ],
    # only valid for cc_library
    # alwayslink = 1,
)

cc_binary(
    name = "libmesh.dylib",
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS,
    linkshared = True,
    visibility = ["//visibility:public"],
    deps = [
        ":mesh-core",
        ":mesh-lib",
    ],
    # only valid for cc_library
    # alwayslink = 1,
)

cc_test(
    name = "unit-tests",
    srcs = glob([
        "unit/*.cc",
    ]),
    copts = [
        "-Isrc",
    ] + MESH_DEFAULT_COPTS,
    linkopts = COMMON_LINKOPTS,
    deps = [
        ":mesh-core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "local-refill-benchmark",
    srcs = ["testing/benchmark/local_refill.cc"],
    copts = [
        "-Isrc",
    ] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    linkopts = COMMON_LINKOPTS,
    deps = [
        ":mesh-core",
        "@com_google_benchmark//:benchmark",
        "@com_google_googletest//:gtest_main",
    ],
)
