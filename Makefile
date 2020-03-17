# Copyright 2020 The Mesh Authors. All rights reserved.
# Use of this source code is governed by the Apache License,
# Version 2.0, that can be found in the LICENSE file.

PREFIX       = /usr
BAZEL_CONFIG =
LIB_SUFFIX   =

UNAME_S = $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIB_EXT      = dylib
BAZEL_PREFIX = darwin
LDCONFIG     =
PREFIX       = /usr/local
else
LIB_EXT      = so
BAZEL_PREFIX = k8
LDCONFIG     = ldconfig
endif

LIB          = mesh
FS_LIB       = libmesh.so
INSTALL_LIB  = libmesh$(LIB_SUFFIX).$(LIB_EXT)

COV_DIR = coverage
CONFIG  = Makefile

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
	./bazel build $(BAZEL_CONFIG) -c opt //src:$(LIB)

test check:
	./bazel test //src:unit-tests --test_output=all --action_env="GTEST_COLOR=1"

install:
	install -c -m 0755 bazel-out/$(BAZEL_PREFIX)-opt/bin/src/$(FS_LIB) $(PREFIX)/lib/$(INSTALL_LIB)
	$(LDCONFIG)
	mkdir -p $(PREFIX)/include/plasma
	install -c -m 0755 src/plasma/mesh.h $(PREFIX)/include/plasma/mesh.h

clang-coverage: $(UNIT_BIN) $(LIB) $(CONFIG)
	mkdir -p "$(COV_DIR)"
	rm -f "$(COV_DIR)/unit.test.profdata"
	cd "$(COV_DIR)" && llvm-profdata merge -sparse ../default.profraw -o unit.test.profdata
	cd "$(COV_DIR)" && llvm-cov show -format=html -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' >index.html
	cd "$(COV_DIR)" && llvm-cov report -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' -use-color
	rm -f default.profraw

benchmark:
	./bazel build --config=disable-meshing --config=debugsymbols -c opt //src:local-refill-benchmark
	./bazel-bin/src/local-refill-benchmark

format:
	clang-format -i src/*.cc src/*.c src/*.h  src/plasma/*.h src/rng/*.h src/static/*.h src/testing/unit/*.cc src/testing/*.cc src/testing/benchmark/*.cc

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

.PHONY: all clean distclean format test test_frag check build benchmark install TAGS
