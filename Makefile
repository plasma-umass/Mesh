include config.mk


SRCS = src/libmesh.cc
OBJS = $(SRCS:.cc=.o)

LIB = libmesh.so

HEAP_LAYERS = Heap-Layers

CONFIG = Makefile config.mk

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS += -s
endif

.SUFFIXES:
.SUFFIXES: .cc .cpp .c .o .d .test

all: $(LIB) run

$(HEAP_LAYERS):
	@echo "  GIT   $@"
	git submodule update --init
	touch $@

%.o: %.c $(CONFIG)
	@echo "  CC    $@"
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.o: %.cc $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(HEAP_LAYERS) $(OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

install: $(LIB)
	install -c -m 0755 $(LIB) $(PREFIX)/lib/$(LIB)
	ldconfig

clean:
	rm -f test/fork-example
	rm -f $(LIB) src/*.o src/*.gcda src/*.gcno src/*.d
	find . -name '*~' -print0 | xargs -0 rm -f

paper:
	$(MAKE) -C paper

test/fork-example: test/fork-example.o $(CONFIG)
	$(CC) -pipe -fno-builtin-malloc -fno-omit-frame-pointer -g -o $@ $< -L$(PWD) -lmesh -Wl,-rpath,"$(PWD)"


run: $(LIB) test/fork-example
	test/fork-example

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | xargs etags -l c++

-include $(OBJS:.o=.d)

.PHONY: all clean install paper run TAGS
