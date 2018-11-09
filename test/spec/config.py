import collections

class Config:
    def __init__(self, name, docker_image, dir_name=None):
        self.name = name
        self.docker_image = docker_image
        if dir_name:
            self.dir_name = dir_name
        else:
            self.dir_name = name

configs = [
    Config('mesh', 'bpowers/spec:mesh-0n', 'mesh-0n'),
    Config('mesh', 'bpowers/spec:mesh-2y', 'mesh-2y'),
    # Config('mesh', 'bpowers/spec:mesh-0y', 'mesh-0y'),
    # Config('mesh', 'bpowers/spec:mesh-1y', 'mesh-1y'),
    # Config('jemalloc', 'bpowers/spec:jemalloc'),
    # Config('glibc', 'bpowers/spec:glibc'),
    # Config('diehard', 'bpowers/spec:diehard'),
    # Config('hoard', 'bpowers/spec:hoard'),
]
