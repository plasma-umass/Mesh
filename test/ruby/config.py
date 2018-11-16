import collections

# Config = collections.namedtuple('Config', ['name', 'use_flags', 'ldflags'])

class Config:
    def __init__(self, name, env=None, skip=False):
        self.name = name
        self.env = env
        self.skip = skip

configs = [
    Config('mesh0n', 'LD_PRELOAD=libmesh0n.so'),
    Config('mesh1n', 'LD_PRELOAD=libmesh1n.so'),
    Config('mesh2n', 'LD_PRELOAD=libmesh2n.so'),
    Config('mesh0y', 'LD_PRELOAD=libmesh0y.so'),
    Config('mesh1y', 'LD_PRELOAD=libmesh1y.so'),
    Config('mesh2y', 'LD_PRELOAD=libmesh2y.so'),
    Config('jemalloc', 'LD_PRELOAD=libjemalloc.so'),
    Config('libc'),
]
