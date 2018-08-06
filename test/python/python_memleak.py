"""
Memory usage of Python < 3.3 grows between some function calls, randomly,
whereas it should stay stable. The final memory usage should be close to the
initial memory usage.

Example with Python 2.6:

    Initial memory:
    VmRSS:            3176 kB
    After call #1:
    VmRSS:            4996 kB
    After call #2:
    VmRSS:            4996 kB
    After call #3:
    VmRSS:           14704 kB
    Finally memory
    VmRSS:           14704 kB

Example with Python 3.3 (compiled in debug mode):

    Initial memory:
    VmRSS:            6048 kB
    After call #1:
    VmRSS:            6732 kB
    After call #2:
    VmRSS:            6732 kB
    After call #3:
    VmRSS:            6732 kB
    Finally memory
    VmRSS:            6732 kB

The Python memory allocator of Python 3.3 uses mmap(), when available, instead
of malloc(). munmap() releases immediatly system memory because it can punch
holes in the memory space of the process, whereas malloc() uses brk() and
sbrk() which uses a contigious address range for the heap memory.

The Python memory allocator allocates chunks of memory of 256 KB (see
ARENA_SIZE in Objects/obmalloc.c). A chunk cannot be released to the system
before all objects stored in the chunk are released.

The Python memory allocator is only used for allocations smaller than 256 bytes
in Python <= 3.2, or allocations smaller than 512 bytes in Python 3.3.
Otherwise, malloc() and free() are used. The GNU libc uses brk() or mmap()
depending on a threshold: 128 KB by default. The threshold is dynamic nowadays.
Use mallopt(M_MMAP_THRESHOLD, nbytes) to change this threshold.

See also:

* http://pushingtheweb.com/2010/06/python-and-tcmalloc/
* http://sourceware.org/ml/libc-alpha/2006-03/msg00033.html
* http://www.linuxdevcenter.com/pub/a/linux/2006/11/30/linux-out-of-memory.html?page=2
* http://cloudfundoo.wordpress.com/2012/05/18/minor-page-faults-and-dynamic-memory-allocation-in-linux/
"""
import gc
import sys

def dump_memory():
    with open("/proc/self/status") as fp:
        for line in fp:
            if "VmRSS" not in line:
                continue
            print(line.rstrip())
            break

    #with open("/proc/self/maps") as fp:
    #    for line in fp:
    #        print(line.rstrip())

def func():
    ns = {}
    codeobj = compile(codestr, 'wastememory.py', "exec")
    exec(codeobj, ns, ns)
    ns.clear()
    codeobj = None
    ns = None
    gc.collect()

codestr = ["""class SuperClass:"""]
for index in range(20000):
    codestr.append("""
    classattr%s = 2

    def methdod%s(self, arg):
        "docstring"
        x = len(arg)
        return x""" % (index, index))
codestr = ''.join(codestr)

print("Initial memory: ")
dump_memory()

for loop in range(1, 8):
    func()
    print("After call #%s:" % loop)
    dump_memory()

print("Finally memory")
dump_memory()

