VERSION = 0.1.0

PREFIX = /usr/local


LCOV     ?= true #lcov
GENHTML  ?= true #genhtml

AR       ?= llvm-ar
RANLIB   ?= llvm-ranlib

CC        = clang
CXX       = clang++


COMMON_FLAGS = \
	-pipe \
	-fno-builtin-malloc \
	-fno-omit-frame-pointer \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables \
	-ffunction-sections \
	-fdata-sections \
	-Wno-gnu-statement-expression \
	-Werror=implicit-function-declaration \
	-Werror=implicit-int \
	-Werror=pointer-sign \
	-Werror=pointer-arith

CPPFLAGS   = -DVERSION=\"$(VERSION)\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700
CFLAG_OPTS = \
	-std=c11 \
	-I Heap-Layers \
	$(CPPFLAGS) \
	$(COMMON_FLAGS)

CXXFLAG_OPTS = \
	-std=c++14 \
	-I impl/Heap-Layers \
	-I impl/include \
	-I impl/include/static \
	$(CPPFLAGS) \
	$(COMMON_FLAGS)

# guard GCC specific flags
ifeq ($(shell if $(CC) --version | grep -q GCC; then echo "gcc"; else echo "other"; fi),gcc)
CFLAG_OPTS += \
	-fexcess-precision=standard \
	-Wa,--noexecstack
endif

# cribbed from musl's args for building libc.so
LINK_FLAGS = $(CFLAG_OPTS) \
	-Wl,--sort-common \
	-Wl,--gc-sections \
	-Wl,--hash-style=both \
	-Wl,--no-undefined \
	-Wl,-Bsymbolic-functions \
	-Wl,-z,now,-z,relro \
	-ftls-model=initial-exec \
	-shared

WARNFLAGS := \
        -Wall -Wextra -pedantic \
        -Wundef \
        -Wno-unused-parameter \
	-Woverloaded-virtual \
	-Werror=return-type \
	-Wtype-limits \
	-Wempty-body \
	-Wno-ctor-dtor-privacy \
	-Winvalid-offsetof \
	-Wvariadic-macros \
	-Wcast-align


OPT       = -O2
DEBUGINFO = -g
#LTO       = -flto

CFLAGS   += $(OPT) -fPIC -pthread $(WARNFLAGS) $(CFLAG_OPTS)
CFLAGS   += $(DEBUGINFO)
CFLAGS   += $(LTO)
#CFLAGS   += -Wunsafe-loop-optimizations

CXXFLAGS   += $(OPT) -fPIC -pthread $(WARNFLAGS) $(CXXFLAG_OPTS)
CXXFLAGS   += $(DEBUGINFO)
CXXFLAGS   += $(LTO)

LIBS      = -lgcc -lgcc_eh -ldl

LDFLAGS  += $(OPT) $(LINK_FLAGS)
LDFLAGS  += $(DEBUGINFO)
LDFLAGS  += $(LTO)
