PREFIX = /usr

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LIB     = libmesh.dylib
else
LIB     = libmesh.so
endif
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

build:
	bazel build -c opt //src:$(LIB)

test check:
	bazel test //src:unit-tests

install:
	install -c -m 0755 bazel-bin/src/$(LIB) $(PREFIX)/lib/$(LIB)
	ldconfig
	mkdir -p $(PREFIX)/include/plasma
	install -c -m 0755 src/plasma/mesh.h $(PREFIX)/include/plasma/mesh.h

clang-coverage: $(UNIT_BIN) $(LIB) $(CONFIG)
	mkdir -p "$(COV_DIR)"
	rm -f "$(COV_DIR)/unit.test.profdata"
	cd "$(COV_DIR)" && llvm-profdata merge -sparse ../default.profraw -o unit.test.profdata
	cd "$(COV_DIR)" && llvm-cov show -format=html -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' >index.html
	cd "$(COV_DIR)" && llvm-cov report -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' -use-color
	rm -f default.profraw

format:
	clang-format -i src/*.cc src/*.c src/*.h  src/plasma/*.h src/rng/*.h src/static/*.h src/test/*.cc src/test/*.cc src/unit/*.cc

clean:
	find . -name '*~' -print0 | xargs -0 rm -f
	bazel clean
	bazel shutdown


distclean: clean
	bazel clean --expunge

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

.PHONY: all clean distclean format test test_frag check build install TAGS
