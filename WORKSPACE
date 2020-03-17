# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

workspace(name = "org_libmesh")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

commit = {
    "llvm_toolchain": "1eb0a56b27bc38489fe9257e7d2010dd64ff3cf9",
    "rules_cc": "d545fa4f798f2a0b82f556b8b0ec59a93c100df7",
    "googletest": "703bd9caab50b139428cea1aaff9974ebee5742e",
    "benchmark": "8e0b1913d4ea803dfeb2e55567208fcab6b1b6c7",
    "heap_layers": "18966daad7a3f2ef30e9a2ac3750b590bb688ba3",
}

http_archive(
    name = "rules_cc",
    sha256 = "188e88d911b69bb77b391e02a4a5fdec2313dd01f0305d97cd3451e38f7ace10",
    strip_prefix = "rules_cc-{}".format(commit["rules_cc"]),
    urls = [
        "https://github.com/bazelbuild/rules_cc/archive/{}.zip".format(commit["rules_cc"]),
    ],
)

http_archive(
    name = "com_grail_bazel_toolchain",
    sha256 = "1bb403ef994f23d6200c8469d1d2bec51ff0556f297b6045c9d75c5b86a7ee5c",
    strip_prefix = "bazel-toolchain-{}".format(commit["llvm_toolchain"]),
    urls = [
        "https://github.com/grailbio/bazel-toolchain/archive/{}.zip".format(commit["llvm_toolchain"]),
    ],
)

load("@com_grail_bazel_toolchain//toolchain:rules.bzl", "llvm_toolchain")

llvm_toolchain(
    name = "llvm_toolchain",
    llvm_version = "9.0.0",
)

load("@llvm_toolchain//:toolchains.bzl", "llvm_register_toolchains")

llvm_register_toolchains()

http_archive(
    name = "com_google_googletest",
    sha256 = "2db427be8b258ad401177c411c2a7c2f6bc78548a04f1a23576cc62616d9cd38",
    strip_prefix = "googletest-{}".format(commit["googletest"]),
    urls = [
        "https://github.com/google/googletest/archive/{}.zip".format(commit["googletest"]),
    ],
)

http_archive(
    name = "com_google_benchmark",
    sha256 = "7eca6b8b82849b3d22b14aae583d327bb13519ea440ddb995c89b8ea468e26b0",
    strip_prefix = "benchmark-{}".format(commit["benchmark"]),
    urls = [
        "https://github.com/google/benchmark/archive/{}.zip".format(commit["benchmark"]),
    ],
)

http_archive(
    name = "org_heaplayers",
    sha256 = "c9058ce705a05ce950744ab161dfd1b8ace598b34c1230820dd9404764886e94",
    strip_prefix = "Heap-Layers-{}".format(commit["heap_layers"]),
    urls = [
        "https://github.com/emeryberger/Heap-Layers/archive/{}.zip".format(commit["heap_layers"]),
    ],
)
