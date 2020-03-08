# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

workspace(name = 'org_libmesh')

load('@bazel_tools//tools/build_defs/repo:http.bzl', 'http_archive')

commit = {
    'llvm_toolchain': '6f99e79bb4f8ad1c8c362745d648197e536826ab',
    'rules_cc': 'd545fa4f798f2a0b82f556b8b0ec59a93c100df7',
    'googletest': '703bd9caab50b139428cea1aaff9974ebee5742e',
    'heap_layers': 'a80041cc15174ab82a39bae1cd750b52955c7eef',
}

http_archive(
    name = 'rules_cc',
    urls = [
        'https://github.com/bazelbuild/rules_cc/archive/{}.zip'.format(commit['rules_cc']),
    ],
    strip_prefix = 'rules_cc-{}'.format(commit['rules_cc']),
    sha256 = '682a0ce1ccdac678d07df56a5f8cf0880fd7d9e08302b8f677b92db22e72052e',
)

http_archive(
    name = 'com_grail_bazel_toolchain',
    urls = [
        'https://github.com/grailbio/bazel-toolchain/archive/{}.zip'.format(commit['llvm_toolchain']),
    ],
    strip_prefix = 'bazel-toolchain-{}'.format(commit['llvm_toolchain']),
    sha256 = '581b7bfb2962a7daf19c4a66b37d97001ab7017126530978aef252615184fef7',
)

load('@com_grail_bazel_toolchain//toolchain:rules.bzl', 'llvm_toolchain')

llvm_toolchain(
    name = 'llvm_toolchain',
    llvm_version = '9.0.0',
)

load('@llvm_toolchain//:toolchains.bzl', 'llvm_register_toolchains')

llvm_register_toolchains()

http_archive(
    name = 'org_heaplayers',
    urls = [
        'https://github.com/google/googletest/archive/{}.zip'.format(commit['googletest']),
    ],
    strip_prefix = 'googletest-{}'.format(commit['googletest']),
)

http_archive(
    name = 'org_heaplayers',
    urls = [
        'https://github.com/emeryberger/Heap-Layers/archive/{}.zip'.format(commit['heap_layers']),
    ],
    strip_prefix = 'Heap-Layers-{}'.format(commit['heap_layers']),
)
