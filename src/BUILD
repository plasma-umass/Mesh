# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

load("//src:copts.bzl", "MESH_DEFAULT_COPTS", "MESH_DEFAULT_LINKOPTS")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])  # Apache 2.0

# used in copts.bzl to set llvm/gcc-only flags
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

config_setting(
    name = "opt_build",
    values = {
        "compilation_mode": "opt",
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
    "@platforms//os:linux": [
        "_DEFAULT_SOURCE",
        "_BSD_SOURCE",
        "_XOPEN_SOURCE=700",
    ],
    "//conditions:default": [],
}) + select({
    ":opt_build": [],
    "//conditions:default": [],
})

COMMON_LINKOPTS = [
    "-lm",
    "-lpthread",
    "-ldl",
] + select({
    "@platforms//os:linux": [
        "-Wl,--no-as-needed",
        "-Wl,--no-add-needed",
        "-Wl,--sort-common",
        "-Wl,--gc-sections",
        "-Wl,--hash-style=both",
        "-Wl,--no-undefined",
        "-Wl,-Bsymbolic-functions",
        "-Wl,-z,now,-z,relro",
        "-ftls-model=initial-exec",
        "-Wl,--exclude-libs,ALL",
        "-static-libstdc++",
        "-static-libgcc",
        "-Wl,-Bstatic",
        "-l:libstdc++.a",
        "-Wl,-Bdynamic",
        "-lrt",
    ],
    "//conditions:default": [],
})

ARCH_LINKOPTS = select({
    "@platforms//cpu:x86_64": [
        "-march=westmere",
        "-mavx2",
    ],
    "@platforms//cpu:aarch64": [
        "-march=armv8.2-a",
    ],
    "//conditions:default": [],
})

# On Linux, use gold linker instead of lld for GCC LTO compatibility.
# Bazel may auto-detect lld and configure it, but GCC's LTO bytecode
# is incompatible with lld (which only supports LLVM's LTO format).
LINKER_FLAGS = select({
    "@platforms//os:linux": ["-fuse-ld=gold"],
    "//conditions:default": [],
})

LTO_LINKOPTS = LINKER_FLAGS + select({
    ":opt_build": ["-flto"],
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
    ] + select({
        "@platforms//os:macos": ["memory_stats_macos.cc"],
        "@platforms//os:linux": ["memory_stats_linux.cc"],
        "//conditions:default": [],
    }),
    hdrs = glob([
        "*.h",
        "plasma/*.h",
        "rng/*.h",
        "gnu_wrapper.cc",
        "mac_wrapper.cc",
        "rpl_printf.c",
        "static/*.h",
        "size_classes.def",
    ]),
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
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
        "mac_wrapper.cc",
        "static/*.h",
        "size_classes.def",
        "wrapper.cc",
    ]),
    # TODO: config options here too
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = True,
    visibility = ["//visibility:private"],
    deps = [
        "@org_heaplayers",
    ],
)

cc_library(
    name = "mesh",
    srcs = [
        "d_assert.cc",
        "global_heap.cc",
        "libmesh.cc",
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
        "mac_wrapper.cc",
        "wrapper.cc",
        "rpl_printf.c",
        "static/*.h",
        "size_classes.def",
    ]),
    copts = NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = False,
    visibility = ["//visibility:public"],
    # disabled, as we directly specify the source files above.
    # this is to work around a bazel bug on macOS where if we
    # specify only deps, it produces an empty dylib
    # deps = [
    #     ":mesh-core",
    #     ":mesh-lib",
    # ],
    deps = [
        "@org_heaplayers",
    ],
    # only valid for cc_library
    alwayslink = 1,
)

cc_library(
    name = "measure_rss",
    srcs = ["measure_rss.cc"],
    hdrs = ["measure_rss.h"],
    copts = MESH_DEFAULT_COPTS,
    visibility = ["//visibility:private"],
)

cc_binary(
    name = "fragmenter",
    srcs = ["testing/fragmenter.cc"],
    copts = ["-Isrc"] + MESH_DEFAULT_COPTS,
    linkopts = MESH_DEFAULT_LINKOPTS + select({
        "@platforms//os:linux": ["-ldl"],
        "//conditions:default": [],
    }),
    deps = [":measure_rss"],
)

cc_test(
    name = "unit-tests",
    size = "small",  # Test runs in ~2.5s
    srcs = glob([
        "testing/unit/*.cc",
    ], exclude = [
        "testing/unit/macos_memory_test.cc",  # Conditionally included below
    ]) + select({
        "@platforms//os:macos": ["testing/unit/macos_memory_test.cc"],
        "//conditions:default": [],
    }),
    copts = [
        "-Isrc",
    ] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,  # Include common defines for consistent behavior
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    deps = [
        ":mesh-core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "local-refill-benchmark",
    srcs = [
        "testing/benchmark/local_refill.cc",
        "testing/benchmark/malloc_benchmark.cc",
    ],
    copts = [
        "-Isrc",
    ] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = True,
    deps = [
        ":mesh",
        "@com_google_benchmark//:benchmark_main",
    ],
)

# Index computation benchmark - compares float reciprocal vs integer magic division
# for computing object index from byte offset (hot path in free())
cc_binary(
    name = "index-compute-benchmark",
    srcs = [
        "testing/benchmark/index_compute_benchmark.cc",
        "size_class_reciprocals.h",
    ],
    copts = [
        "-Isrc",
    ] + MESH_DEFAULT_COPTS,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LINKER_FLAGS,
    linkstatic = True,
    deps = [
        "@com_google_benchmark//:benchmark",
    ],
)

# Larson benchmark - multi-threaded allocation stress test
# This benchmark exercises the "remote free" path where threads free memory
# allocated by other threads. Use --config=disable-meshing for nomesh variant.
cc_binary(
    name = "larson-benchmark",
    srcs = [
        "testing/benchmark/larson.cc",
    ],
    copts = [
        "-Isrc",
    ] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = True,
    deps = [
        ":mesh",
    ],
)

MESH_SHARED_SRCS = [
    "d_assert.cc",
    "global_heap.cc",
    "libmesh.cc",
    "measure_rss.cc",
    "meshable_arena.cc",
    "real.cc",
    "runtime.cc",
    "thread_local_heap.cc",
]

MESH_SHARED_HDRS = glob([
    "*.h",
    "plasma/*.h",
    "rng/*.h",
    "static/*.h",
])

cc_library(
    name = "mesh-textual-hdrs",
    hdrs = MESH_SHARED_HDRS,
    textual_hdrs = [
        "rpl_printf.c",
        "size_classes.def",
        "wrapper.cc",
        "gnu_wrapper.cc",
        "mac_wrapper.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "@org_heaplayers",
    ],
)

cc_binary(
    name = "libmesh.dylib",
    srcs = MESH_SHARED_SRCS + [
        "memory_stats_macos.cc",
    ],
    copts = ["-Isrc"] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkshared = True,
    target_compatible_with = ["@platforms//os:macos"],
    visibility = ["//visibility:public"],
    deps = [
        ":mesh-textual-hdrs",
    ],
)

cc_binary(
    name = "libmesh.so",
    srcs = MESH_SHARED_SRCS + [
        "memory_stats_linux.cc",
    ],
    copts = ["-Isrc"] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkshared = True,
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:public"],
    deps = [
        ":mesh-textual-hdrs",
    ],
)

cc_library(
    name = "mesh-static-macos",
    srcs = MESH_SHARED_SRCS + [
        "memory_stats_macos.cc",
    ],
    copts = ["-Isrc"] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = True,
    target_compatible_with = ["@platforms//os:macos"],
    visibility = ["//visibility:private"],
    deps = [
        ":mesh-textual-hdrs",
    ],
)

cc_library(
    name = "mesh-static-linux",
    srcs = MESH_SHARED_SRCS + [
        "memory_stats_linux.cc",
    ],
    copts = ["-Isrc"] + NO_BUILTIN_MALLOC + MESH_DEFAULT_COPTS,
    defines = COMMON_DEFINES,
    linkopts = COMMON_LINKOPTS + ARCH_LINKOPTS + LTO_LINKOPTS,
    linkstatic = True,
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:private"],
    deps = [
        ":mesh-textual-hdrs",
    ],
)

cc_static_library(
    name = "mesh_static_macos",
    deps = [":mesh-static-macos"],
    target_compatible_with = ["@platforms//os:macos"],
    visibility = ["//visibility:public"],
)

cc_static_library(
    name = "mesh_static_linux",
    deps = [":mesh-static-linux"],
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:public"],
)
