workspace(name = "org_libmesh")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

llvm_toolchain_commit = "df0f2eb6fe698b4483bb2d5b5670c17ac69c6362"

googletest_commit = "f2fb48c3b3d79a75a88a99fba6576b25d42ec528"

heaplayers_commit = "fbbf8b46e608e6b9e0a4ee2e180a243c5a15d743"

http_archive(
    name = "com_grail_bazel_toolchain",
    sha256 = "105bfaf1355fb1d98781000a2986fd50424e1923e951d75aa6d3bbec6dba6907",
    strip_prefix = "bazel-toolchain-" + llvm_toolchain_commit,
    urls = ["https://github.com/grailbio/bazel-toolchain/archive/{}.tar.gz".format(llvm_toolchain_commit)],
)

load("@com_grail_bazel_toolchain//toolchain:rules.bzl", "llvm_toolchain")

llvm_toolchain(
    name = "llvm_toolchain",
    llvm_version = "8.0.0",
)

load("@llvm_toolchain//:toolchains.bzl", "llvm_register_toolchains")

llvm_register_toolchains()

git_repository(
    name = "com_google_googletest",
    commit = googletest_commit,
    remote = "https://github.com/google/googletest.git",
    shallow_since = "1565193450 -0400",
)

git_repository(
    name = "org_heaplayers",
    commit = heaplayers_commit,
    remote = "https://github.com/bpowers/Heap-Layers.git",
    shallow_since = "1568864248 -0700",
)
