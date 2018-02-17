-include config.mk

ifeq ($(VERSION),)

all:
	@echo "run ./configure before running make."
	@exit 1

else

PREFIX = /usr

ARCH             = x86_64

COMMON_SRCS      = src/runtime.cc src/meshing.cc src/meshable-arena.cc src/d_assert.cc

LIB_SRCS         = $(COMMON_SRCS) src/libmesh.cc
LIB_OBJS         = $(addprefix build/,$(patsubst %.c,%.o,$(patsubst %.S,%.o,$(LIB_SRCS:.cc=.o))))
LIB              = libmesh.so

GTEST_SRCS       = src/vendor/googletest/googletest/src/gtest-all.cc \
                   src/vendor/googletest/googletest/src/gtest_main.cc

UNIT_SRCS        = $(wildcard src/unit/*.cc) $(GTEST_SRCS) $(COMMON_SRCS)
UNIT_OBJS        = $(addprefix build/,$(patsubst %.c,%.o,$(UNIT_SRCS:.cc=.o)))
UNIT_CXXFLAGS    = -isystem src/vendor/googletest/googletest/include -Isrc/vendor/googletest/googletest $(filter-out -Wextra,$(CXXFLAGS:-Wundef=)) -Wno-unused-const-variable -DGTEST_HAS_PTHREAD=1
UNIT_LDFLAGS     = $(LIBS)
UNIT_BIN         = unit.test

BENCH_SRCS       = src/meshing_benchmark.cc $(COMMON_SRCS)
BENCH_OBJS       = $(addprefix build/,$(patsubst %.c,%.o,$(BENCH_SRCS:.cc=.o)))
BENCH_BIN        = meshing-benchmark

FRAG_SRCS        = src/fragmenter.cc src/measure_rss.c
FRAG_OBJS        = $(addprefix build/,$(patsubst %.c,%.o,$(FRAG_SRCS:.cc=.o)))
FRAG_BIN         = fragmenter

ALL_OBJS         = $(LIB_OBJS) $(UNIT_OBJS) $(BENCH_OBJS) $(FRAG_OBJS)

# reference files in each subproject to ensure git fully checks the project out
HEAP_LAYERS      = src/vendor/Heap-Layers/heaplayers.h
GTEST            = src/vendor/googletest/googletest/include/gtest/gtest.h
GFLAGS           = src/vendor/gflags/CMakeLists.txt

GFLAGS_BUILD_DIR = build/src/vendor/gflags
GFLAGS_BUILD     = $(GFLAGS_BUILD_DIR)/Makefile
GFLAGS_LIB       = $(GFLAGS_BUILD_DIR)/lib/libgflags.a

ALL_SUBMODULES   = $(HEAP_LAYERS) $(GTEST) $(GFLAGS)

CONFIG           = Makefile config.mk

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS       += -s
endif

.SUFFIXES:
.SUFFIXES: .cc .cpp .S .c .o .d .test

all: test $(BENCH_BIN) $(LIB) $(FRAG_BIN)

build:
	mkdir -p build
	mkdir -p $(basename $(ALL_OBJS))

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

$(GFLAGS_BUILD): $(GFLAGS) $(CONFIG)
	@echo "  CMAKE $@"
	mkdir -p $(GFLAGS_BUILD_DIR)
	cd $(GFLAGS_BUILD_DIR) && CC=$(CC) CXX=$(CXX) cmake $(realpath $(dir $(GFLAGS)))
	touch -c $(GFLAGS_BUILD)

$(GFLAGS_LIB): $(GFLAGS_BUILD) $(CONFIG)
	@echo "  LD    $@"
	cd $(GFLAGS_BUILD_DIR) && $(MAKE)
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

build/src/%.o: src/%.cc build $(CONFIG) $(GFLAGS_LIB)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

build/src/unit/%.o: src/unit/%.cc build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(UNIT_CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(HEAP_LAYERS) $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS)

$(BENCH_BIN): $(HEAP_LAYERS) $(GFLAGS_LIB) $(BENCH_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) $(LDFLAGS) -o $@ $(BENCH_OBJS) $(LIBS) -L$(GFLAGS_BUILD_DIR)/lib -lgflags

$(FRAG_BIN): $(GFLAGS_LIB) $(FRAG_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) $(LDFLAGS) -o $@ $(FRAG_OBJS) $(LIBS) -L$(GFLAGS_BUILD_DIR)/lib -lgflags

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

paper:
	$(MAKE) -C paper

src/test/fork-example: build/src/test/fork-example.o $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"

src/test/thread-example: src/test/thread.cc $(CONFIG)
	$(CXX) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)" -lpthread


run: $(LIB) src/test/fork-example
	src/test/fork-example

format:
	clang-format -i src/*.cc src/*.h

endif

clean:
	rm -f src/test/fork-example
	rm -f $(UNIT_BIN) $(BENCH_BIN) $(LIB)
	find . -name '*~' -print0 | xargs -0 rm -f
	rm -rf build


distclean: clean
	rm -rf $(GFLAGS_BUILD_DIR)

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

-include $(ALL_OBJS:.o=.d)

.PHONY: all clean distclean format test test_frag check install paper run TAGS
