# For Windows; see GNUmakefile for the Unix Makefile

CC = cl
CFLAGS = /Ox # -O0 -DSANITIZER_GO=0 -march=westmere -fPIC -pipe -fno-builtin-malloc -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith -Wall -Wextra -pedantic -Wno-unused-parameter -Wno-unused-variable -Werror=return-type -Wtype-limits -Wempty-body -Wvariadic-macros -Wcast-align -Isrc/vendor/googletest/googletest/include -Isrc/vendor/googletest/googletest -isystem build/src/vendor/gflags/include -Isrc/vendor/Heap-Layers -Wa,--noexecstack
CXXFLAGS = /std:c++17 /Isrc /Isrc\vendor\Heap-Layers $(CFLAGS) # -Woverloaded-virtual -Wno-ctor-dtor-privacy -Winvalid-offsetof -std=c++1z -I src $(CFLAGS)
LDFLAGS = # -march=westmere
LIBS = /Md # -lm -lpthread -ldl
VERSION = 0.1.0

# LIB = libmesh.dll
SHARED_LIB = libmesh.dll

PREFIX = /usr

ARCH             = x86_64

COMMON_SRCS      = src/thread_local_heap.cc src/global_heap.cc src/runtime.cc src/real.cc src/meshable_arena.cc src/d_assert.cc src/measure_rss.cc

src/thread_local_heap.o: src/thread_local_heap.cc
	$(CC) $(CXXFLAGS) /c src/thread_local_heap.cc /o src/thread_local_heap.o

LIB_SRCS         = $(COMMON_SRCS) src/libmesh.cc
LIB_OBJS         = src/thread_local_heap.o src/global_heap.o src/runtime.o src/real.o src/meshable_arena.o src/d_assert.o src/measure_rss.o src/libmesh.o

GTEST_SRCS       = src/vendor/googletest/googletest/src/gtest-all.cc \
                   src/vendor/googletest/googletest/src/gtest_main.cc

UNIT_LDFLAGS     = $(LIBS)
UNIT_BIN         = unit.test

BENCH_SRCS       = src/meshing_benchmark.cc $(COMMON_SRCS)
#BENCH_OBJS       = $(addprefix build/,$(patsubst %.c,%.o,$(BENCH_SRCS:.cc=.o)))
BENCH_BIN        = meshing-benchmark

FRAG_SRCS        = src/fragmenter.cc src/measure_rss.cc
#FRAG_OBJS        = $(addprefix build/,$(patsubst %.c,%.o,$(FRAG_SRCS:.cc=.o)))
FRAG_BIN         = fragmenter

ALL_OBJS         = $(LIB_OBJS) $(UNIT_OBJS) $(BENCH_OBJS) $(FRAG_OBJS)

# reference files in each subproject to ensure git fully checks the project out
HEAP_LAYERS      = src/vendor/Heap-Layers/heaplayers.h
GTEST            = src/vendor/googletest/googletest/include/gtest/gtest.h

COV_DIR          = coverage

ALL_SUBMODULES   = $(HEAP_LAYERS) $(GTEST)

CONFIG           = Makefile config.mk

.SUFFIXES:
.SUFFIXES: .cc .c .o .d .test

all: $(SHARED_LIB) # test $(LIB) $(FRAG_BIN)

build:
	mkdir -p build
#	mkdir -p $(basename $(ALL_OBJS))

test_frag: $(FRAG_BIN) $(LIB)
	@echo "  GLIBC MALLOC"
	# ./$(FRAG_BIN)
	@echo ""
	@echo ""
	@echo "  TCMALLOC MALLOC"
	# LD_PRELOAD=libtcmalloc_minimal.so ./$(FRAG_BIN)
	@echo ""
	@echo ""
	@echo "  MESH MALLOC"
	LD_PRELOAD=./$(LIB) ./$(FRAG_BIN)

$(ALL_SUBMODULES):
	@echo "  GIT   $@"
	git submodule update --init
	touch -c $@

build/src/vendor/googletest/%.o: src/vendor/googletest/%.cc build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(UNIT_CXXFLAGS) -MMD -o $@ -c $<

build/src/%.o: src/%.S build $(CONFIG)
	@echo "  AS    $@"
	$(CC) -O$(O) $(ASFLAGS) -o $@ -c $<

build/src/%.o: src/%.c build $(CONFIG)
	@echo "  CC    $@"
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

build/src/%.o: src/%.cc build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

build/src/unit/%.o: src/unit/%.cc build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(UNIT_CXXFLAGS) -MMD -o $@ -c $<

$(SHARED_LIB): $(HEAP_LAYERS) $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS)

libmesh.dylib: $(HEAP_LAYERS) $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -compatibility_version 1 -current_version 1 -dynamiclib $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS)

$(BENCH_BIN): $(HEAP_LAYERS) $(BENCH_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) $(LDFLAGS) -o $@ $(BENCH_OBJS) $(LIBS)

$(FRAG_BIN): $(GFLAGS_LIB) $(FRAG_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) $(LDFLAGS) -o $@ $(FRAG_OBJS) $(LIBS)

$(UNIT_BIN): $(CONFIG) $(UNIT_OBJS)
	@echo "  LD    $@"
	$(CXX) -O0 $(LDFLAGS) -o $@ $(UNIT_OBJS) $(UNIT_LDFLAGS)

test check: $(UNIT_BIN)
	./$(UNIT_BIN)

install: $(LIB)
	install -c -m 0755 $(LIB) $(PREFIX)/lib/$(LIB)
	ldconfig
	mkdir -p $(PREFIX)/include/plasma
	install -c -m 0755 src/plasma/mesh.h $(PREFIX)/include/plasma/mesh.h

lib: $(LIB)

paper:
	$(MAKE) -C paper

coverage: $(UNIT_BIN) $(LIB) $(CONFIG)
	mkdir -p "$(COV_DIR)"
	cd "$(COV_DIR)" && lcov --base-directory .. --directory ../build/src --capture --output-file app.info
	cd "$(COV_DIR)" && genhtml app.info

clang-coverage: $(UNIT_BIN) $(LIB) $(CONFIG)
	mkdir -p "$(COV_DIR)"
	rm -f "$(COV_DIR)/unit.test.profdata"
	cd "$(COV_DIR)" && llvm-profdata merge -sparse ../default.profraw -o unit.test.profdata
	cd "$(COV_DIR)" && llvm-cov show -format=html -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' >index.html
	cd "$(COV_DIR)" && llvm-cov report -instr-profile=unit.test.profdata ../unit.test -ignore-filename-regex='.*(vendor|unit)/.*' -use-color
	rm -f default.profraw

src/test/fork-example: build/src/test/fork-example.o $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"

src/test/local-alloc: src/test/local-alloc.c $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"

src/test/big-alloc: src/test/big-alloc.c $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"

src/test/big-alloc-hoard: src/test/big-alloc.c $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lhoard -Wl,-rpath,"$(PWD)"

src/test/big-alloc-glibc: src/test/big-alloc.c $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -Wl,-rpath,"$(PWD)"

src/test/thread-example: src/test/thread.cc $(CONFIG)
	$(CXX) -std=c++11 -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)" -lpthread


run: $(LIB) src/test/fork-example
	src/test/fork-example

format:
	clang-format -i src/*.cc src/*.c src/*.h  src/plasma/*.h src/rng/*.h src/static/*.h src/test/*.cc src/test/*.cc src/unit/*.cc


clean:
	rm -f src/test/fork-example
	rm -f $(UNIT_BIN) $(BENCH_BIN) $(FRAG_BIN) $(LIB)
	find . -name '*~' -print0 | xargs -0 rm -f
	rm -rf build


distclean: clean

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

.PHONY: all clean distclean format test test_frag check lib install paper run TAGS
