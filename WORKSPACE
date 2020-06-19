# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

workspace(name = "org_libmesh")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

commit = {
    "llvm_toolchain": "58327cd93a8115f999a08c2120156393cc41d98f",
    "rules_cc": "5cbd3dfbd1613f71ef29bbb7b10310b81e272975",
    "googletest": "c6e309b268d4fb9138bed7d0f56b7709c29f102f",
    "benchmark": "15e6dfd7182b74b4fb6860f52fe314d0654307fb",
    "heap_layers": "08ca96cfe11b1dd1c504fb7be613ad00756d568f",
}

http_archive(
    name = "rules_cc",
    sha256 = "d6775fe03da086dfe47c668f54e77e220aa6e601a66a4517eaf19fa1d9fda309",
    strip_prefix = "rules_cc-{}".format(commit["rules_cc"]),
    urls = [
        "https://github.com/bazelbuild/rules_cc/archive/{}.zip".format(commit["rules_cc"]),
    ],
)

http_archive(
    name = "com_grail_bazel_toolchain",
    sha256 = "6a334558d39c9ba09f79aadbb254bb8a670b33ade5a200864c79a5ab8176fe5e",
    strip_prefix = "bazel-toolchain-{}".format(commit["llvm_toolchain"]),
    urls = [
        "https://github.com/grailbio/bazel-toolchain/archive/{}.zip".format(commit["llvm_toolchain"]),
    ],
)

load("@com_grail_bazel_toolchain//toolchain:rules.bzl", "llvm_toolchain")

llvm_toolchain(
    name = "llvm_toolchain",
    llvm_version = "10.0.0",
)

load("@llvm_toolchain//:toolchains.bzl", "llvm_register_toolchains")

llvm_register_toolchains()

http_archive(
    name = "com_google_googletest",
    sha256 = "ac0b859e4c28af555657c7702523465756eb56f8606bf53e62fd934c86b5fa5f",
    strip_prefix = "googletest-{}".format(commit["googletest"]),
    urls = [
        "https://github.com/google/googletest/archive/{}.zip".format(commit["googletest"]),
    ],
)

http_archive(
    name = "com_google_benchmark",
    # sha256 = "",
    strip_prefix = "benchmark-{}".format(commit["benchmark"]),
    urls = [
        "https://github.com/google/benchmark/archive/{}.zip".format(commit["benchmark"]),
    ],
)

http_archive(
    name = "org_heaplayers",
    sha256 = "c8a9f7589e13112515ba1ac8647b4e80462f18a6773f7f5f132a7d7602fe2aec",
    strip_prefix = "Heap-Layers-{}".format(commit["heap_layers"]),
    urls = [
        "https://github.com/emeryberger/Heap-Layers/archive/{}.zip".format(commit["heap_layers"]),
    ],
)
