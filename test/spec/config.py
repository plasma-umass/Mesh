import collections

class Config:
    def __init__(self, name, docker_image):
        self.name = name
        self.docker_image = docker_image

configs = [
    Config('mesh', 'bpowers/spec:mesh'),
    Config('jemalloc', 'bpowers/spec:jemalloc'),
    Config('glibc', 'bpowers/spec:glibc'),
    Config('diehard', 'bpowers/spec:diehard'),
    Config('hoard', 'bpowers/spec:hoard'),
]
