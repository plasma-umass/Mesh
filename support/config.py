# Copyright 2017 Bobby Powers. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

from sys import stderr, argv
from subprocess import Popen, PIPE
from os import environ, getcwd
from datetime import datetime
from os.path import dirname, samefile, join
from shutil import copyfile

LOCAL_PKGCONFIG = '/usr/local/lib/pkgconfig'
PKG_PATH = 'PKG_CONFIG_PATH'

# workaround because apparently the local/bin pkg-config path isn't in
# the system package-config search path, like I think it is on debian-
# based systems.
if PKG_PATH in environ:
    new_path = '%s:%s' % (LOCAL_PKGCONFIG, environ[PKG_PATH])
else:
    new_path = LOCAL_PKGCONFIG
environ[PKG_PATH] = new_path


def slurp(fname):
    with open(fname, 'r') as f:
        return f.read()


def run_cmd(cmd, effect='stdout'):
    '''
    Runs a shell command, waits for it to complete, and returns stdout.
    '''
    with open('/dev/null', 'w') as dev_null:
        call = Popen(cmd, shell=True, stdout=PIPE, stderr=dev_null)
        ret, _ = call.communicate()
        if effect == 'stdout':
            return ret
        else:
            return call.returncode


def exe_available(cmd):
    '''
    Returns true if the command is found on the path.
    '''
    path = run_cmd('which %s' % (cmd))
    return len(path) > 0


def _new_env():
    return {
        'cflags': '',
        'ldflags': '-lrt',
        'libs': '',
    }

help_text = '''Usage: ./configure [ OPTIONS ]
Configure the build system for Mesh.

Options:

  --help      display this help and exit
  --debug     build with debugging symbols
  --profile   build with gcov profiling support
  --optimize  build with heavy optimizations
  --mingw     cross-compiling under mingw32

Report bugs to <bpowers@cs.umass.edu>.
'''

class ConfigBuilder:
    def __init__(self):
        self.env = _new_env()
        self.defs = {}
        self.defs['year'] = str(datetime.now().year)
        self.debug_build = '--debug' in argv
        self.profile_build = '--profile' in argv
        self.optimize_build = '--optimize' in argv
        self.pkg_config = 'pkg-config'
        self.cross_compiling = False
        if '--mingw' in argv:
            self.pkg_config = 'mingw32-pkg-config'
            self.cross_compiling = True

        if '--help' in argv:
            print help_text
            exit(0)

    def config(self, key, val):
        self.defs[key] = val

    def require(self, lib='', program=None):
        if not program:
            program = self.pkg_config

        env = self.env
        path = run_cmd('which %s' % (program))
        if len(path) is 0:
            print >> stderr, 'required program "%s" not found.' % program
            exit(1)

        # single-quote stuff for pkg-config
        if len(lib) > 0:
            lib = "'%s'" % lib

        if len(lib) > 0:
            # assumes a pkg-config-like program
            ret = run_cmd('%s --exists %s' % (program, lib), 'returncode')
            if ret != 0:
                print >> stderr, 'required library %s not found' % lib
                exit(1)

        for info in ['cflags', 'libs', 'ldflags']:
            if info in env:
                existing = env[info]
            else:
                existing = ''
            new = run_cmd('%s --%s %s' % (program, info, lib)).strip()
            env[info] = existing + ' ' + new

    def generate(self, fname='config.mk'):
        if self.profile_build:
            self.append('cflags', '-D_PROF')
        with open(fname, 'w') as config:
            for info in sorted(self.env.keys()):
                values = self.env[info].strip()
                if self.cross_compiling:
                    values = values.replace('-pthread', '-lpthread')
                    values = values.replace('-lrt', '')
                    if info.upper() == 'LIBS':
                        values += ' -lws2_32'
                config.write('%s := %s\n' % (info.upper(),
                                             values))
            if self.cross_compiling:
                config.write('EXE_SUFFIX := .exe')
        if self.cross_compiling:
            with open('config.sh', 'w') as config:
                config.write('EXE_SUFFIX=\'.exe\'')

        src_dir = dirname(argv[0])
        if not samefile(src_dir, getcwd()):
            copyfile(join(src_dir, 'Makefile'), 'Makefile')


    def append(self, var, val):
        self.env[var] = self.env.get(var, '') + ' ' + val

    def prefer(self, cmd, preferred):
        if exe_available(preferred):
            self.env[cmd] = preferred
        else:
            print 'warning: %s not found, using default cc' % preferred
