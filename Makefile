# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

PREFIX       = /usr
LIB_SUFFIX   =

UNAME_S = $(shell uname -s)
UNAME_M = $(shell uname -m)

# Set BAZEL_CONFIG based on architecture
ifeq ($(UNAME_M),x86_64)
BAZEL_CONFIG = --config=modern-amd64
else ifeq ($(UNAME_M),amd64)
BAZEL_CONFIG = --config=modern-amd64
else
# ARM64 and other architectures - no special flags
BAZEL_CONFIG =
endif

ifeq ($(UNAME_S),Darwin)
LIB_EXT        = dylib
STATIC_TARGET  = mesh_static_macos
LDCONFIG       =
PREFIX         = /usr/local
else
LIB_EXT        = so
STATIC_TARGET  = mesh_static_linux
LDCONFIG       = ldconfig
endif

DYNAMIC_LIB    = libmesh.$(LIB_EXT)
STATIC_LIB     = lib$(STATIC_TARGET).a
INSTALL_DYNAMIC = libmesh$(LIB_SUFFIX).$(LIB_EXT)
INSTALL_STATIC  = libmesh$(LIB_SUFFIX).a

COV_DIR = coverage
CONFIG  = Makefile

# clang-format sources (exclude gnulib's rpl_printf.c)
FORMAT_SRCS := $(filter-out src/rpl_printf.c, \
	$(wildcard src/*.cc src/*.c src/*.h) \
	$(wildcard src/plasma/*.h) \
	$(wildcard src/rng/*.h) \
	$(wildcard src/static/*.h) \
	$(wildcard src/testing/unit/*.cc) \
	$(wildcard src/testing/*.cc) \
	$(wildcard src/testing/benchmark/*.cc))

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS       += -s
endif

.SUFFIXES:
.SUFFIXES: .cc .c .o .d .test

all: test build

build lib:
	./bazel build $(BAZEL_CONFIG) -c opt //src:$(DYNAMIC_LIB) //src:$(STATIC_TARGET)

test check:
	./bazel test $(BAZEL_CONFIG) //src:unit-tests --test_output=all --action_env="GTEST_COLOR=1"

install:
	install -c -m 0755 bazel-bin/src/$(DYNAMIC_LIB) $(PREFIX)/lib/$(INSTALL_DYNAMIC)
	install -c -m 0644 bazel-bin/src/$(STATIC_LIB) $(PREFIX)/lib/$(INSTALL_STATIC)
	$(LDCONFIG)
	mkdir -p $(PREFIX)/include/plasma
	install -c -m 0755 src/plasma/mesh.h $(PREFIX)/include/plasma/mesh.h

clang-coverage: $(UNIT_BIN) $(CONFIG)
	mkdir -p "$(COV_DIR)"
	rm -f "$(COV_DIR)/unit.test.profdata"
	cd "$(COV_DIR)" && llvm-profdata merge -sparse ../default.profraw -o unit.test.profdata
	cd "$(COV_DIR)" && llvm-cov show -format=html -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' >index.html
	cd "$(COV_DIR)" && llvm-cov report -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' -use-color
	rm -f default.profraw

benchmark:
	./bazel build $(BAZEL_CONFIG) -c opt //src:fragmenter //src:$(DYNAMIC_LIB)
ifeq ($(UNAME_S),Darwin)
	DYLD_INSERT_LIBRARIES=./bazel-bin/src/$(DYNAMIC_LIB) ./bazel-bin/src/fragmenter
else
	LD_PRELOAD=./bazel-bin/src/$(DYNAMIC_LIB) ./bazel-bin/src/fragmenter
endif
	./bazel build $(BAZEL_CONFIG) --config=disable-meshing --config=nolto -c opt //src:local-refill-benchmark
	./bazel-bin/src/local-refill-benchmark

# Index computation benchmark - compares float reciprocal vs integer magic division
# Run with: make index-benchmark
index-benchmark:
	./bazel build $(BAZEL_CONFIG) -c opt //src:index-compute-benchmark
	./bazel-bin/src/index-compute-benchmark

# Larson benchmark - multi-threaded allocation stress test
# Default runs with meshing disabled for baseline comparison
# Args: sleep_sec min_size max_size chunks_per_thread num_rounds seed num_threads
LARSON_ARGS = 2 8 1000 5000 100 4141 8
PERF_FREQ = 997
FLAMEGRAPH_DIR = third_party/FlameGraph

larson: larson-nomesh

larson-mesh:
	./bazel build $(BAZEL_CONFIG) --config=nolto -c opt //src:larson-benchmark
ifeq ($(UNAME_S),Linux)
	perf record -F $(PERF_FREQ) -g --call-graph fp -o perf-larson-mesh.data -- ./bazel-bin/src/larson-benchmark $(LARSON_ARGS)
	perf script -i perf-larson-mesh.data | $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl | $(FLAMEGRAPH_DIR)/flamegraph.pl --title "larson-mesh" > flamegraph-larson-mesh.svg
	@echo "Flamegraph written to flamegraph-larson-mesh.svg"
else
	./bazel-bin/src/larson-benchmark $(LARSON_ARGS)
endif

larson-nomesh:
	./bazel build $(BAZEL_CONFIG) --config=disable-meshing --config=nolto -c opt //src:larson-benchmark
ifeq ($(UNAME_S),Linux)
	perf record -F $(PERF_FREQ) -g --call-graph fp -o perf-larson-nomesh.data -- ./bazel-bin/src/larson-benchmark $(LARSON_ARGS)
	perf script -i perf-larson-nomesh.data | $(FLAMEGRAPH_DIR)/stackcollapse-perf.pl | $(FLAMEGRAPH_DIR)/flamegraph.pl --title "larson-nomesh" > flamegraph-larson-nomesh.svg
	@echo "Flamegraph written to flamegraph-larson-nomesh.svg"
else
	./bazel-bin/src/larson-benchmark $(LARSON_ARGS)
endif

format:
	clang-format -i $(FORMAT_SRCS)

clean:
	find . -name '*~' -print0 | xargs -0 rm -f
	./bazel clean
	./bazel shutdown


distclean: clean
	./bazel clean --expunge

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

.PHONY: all clean distclean format test test_frag check build benchmark index-benchmark install TAGS larson larson-mesh larson-nomesh
