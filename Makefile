include config.mk


LIB_SRCS = impl/libmesh.cc impl/d_assert.cc
LIB_OBJS = $(LIB_SRCS:.cc=.o)
LIB = libmesh.so

UNIT_SRCS = $(wildcard impl/unit/*.cc) impl/d_assert.cc
UNIT_OBJS = $(UNIT_SRCS:.cc=.o)
UNIT_CXXFLAGS = -isystem impl/vendor/googletest/googletest/include -Iimpl/vendor/googletest/googletest $(filter-out -Wextra,$(CXXFLAGS:-Wundef=)) -Wno-unused-const-variable -DGTEST_HAS_PTHREAD=1
UNIT_LDFLAGS = -lpthread -lunwind
UNIT_BIN = unit.test



ALL_OBJS = $(LIB_OBJS)

# reference files in each subproject to ensure git fully checks the project out
HEAP_LAYERS = impl/vendor/Heap-Layers/heaplayers.h
GTEST       = impl/vendor/googletest/googletest/include/gtest/gtest.h
GFLAGS      = impl/vendor/gflags/src/gflags.cc

ALL_SUBMODULES = $(HEAP_LAYERS) $(GTEST) $(GFLAGS)

CONFIG = Makefile config.mk

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS += -s
endif

.SUFFIXES:
.SUFFIXES: .cc .cpp .c .o .d .test

all: $(LIB) unittests

$(ALL_SUBMODULES):
	@echo "  GIT   $@"
	git submodule update --init
	touch -c $@

%.o: %.c $(CONFIG)
	@echo "  CC    $@"
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.o: %.cc $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

impl/unit/%.o: impl/unit/%.cc $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(UNIT_CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(HEAP_LAYERS) $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS)

$(UNIT_BIN): $(CONFIG) $(UNIT_OBJS)
	@echo "  LD    $@"
	$(CXX) -O0 $(LDFLAGS) -o $@ $(UNIT_OBJS) $(UNIT_LDFLAGS)

unittests: $(UNIT_BIN)
	./$(UNIT_BIN)

install: $(LIB)
	install -c -m 0755 $(LIB) $(PREFIX)/lib/$(LIB)
	ldconfig

clean:
	rm -f impl/test/fork-example
	rm -f $(UNIT_BIN) $(LIB) impl/*.gcda impl/*.gcno
	find . -name '*.o' -print0 | xargs -0 rm -f
	find . -name '*.d' -print0 | xargs -0 rm -f
	find . -name '*~' -print0 | xargs -0 rm -f

paper:
	$(MAKE) -C paper

impl/test/fork-example: impl/test/fork-example.o $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"


run: $(LIB) impl/test/fork-example
	impl/test/fork-example

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

-include $(ALL_OBJS:.o=.d)

.PHONY: all clean unittests install paper run TAGS
