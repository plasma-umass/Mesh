# Copyright 2018 Bobby Powers. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

import argparse
from sys import stdout, stderr, argv
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


class ConfigBuilder:
    def __init__(self):
        self.env = {
            'cflags': '',
            'ldflags': '',
            'libs': '',
        }
        self.defs = {}
        self.config('year', str(datetime.now().year))

        parser = argparse.ArgumentParser(description='Configure the mesh build.')
        parser.add_argument('--debug', action='store_true', default=True,
                            help='build with debugging symbols (default)')
        parser.add_argument('--no-debug', action='store_false', dest='debug',
                            help='build with debugging symbols')
        parser.add_argument('--optimize', action='store_true', default=True,
                            help='build with optimizations (default)')
        parser.add_argument('--no-optimize', action='store_false', dest='optimize',
                            help='build without optimizations')
        parser.add_argument('--gcov', action='store_true', default=False,
                            help='build with gcov profiling support')
        parser.add_argument('--clangcov', action='store_true', default=False,
                            help='build with gcov profiling support')

        parser.add_argument('--randomization', choices=[0, 1, 2], type=int, default=2,
                            help='0: no randomization. 1: freelist init only.  2: freelist init + free fastpath (default)')
        parser.add_argument('--disable-meshing', action='store_true', default=False,
                            help='disable meshing')
        parser.add_argument('--suffix', action='store_true', default=False,
                            help='always suffix the mesh binary with randomization + meshing info')

        args = parser.parse_args()

        self.debug_build = args.debug
        self.gcov_build = args.gcov
        self.clangcov_build = args.clangcov
        self.optimize_build = args.optimize

        self.pkg_config = 'pkg-config'

        if args.randomization == 0:
            self.config_int('shuffle-on-init', 0)
            self.config_int('shuffle-on-free', 0)
        elif args.randomization == 1:
            self.config_int('shuffle-on-init', 1)
            self.config_int('shuffle-on-free', 0)
        elif args.randomization == 2:
            self.config_int('shuffle-on-init', 1)
            self.config_int('shuffle-on-free', 1)
        else:
            raise 'unknown randomization: {}'.format(args.randomization)

        if args.disable_meshing:
            self.config_int('meshing-enabled', 0)
        else:
            self.config_int('meshing-enabled', 1)

        if args.suffix:
            suffix = str(args.randomization)
            if args.disable_meshing:
                suffix = suffix + 'n'
            else:
                suffix = suffix + 'y'
            self.append('lib_suffix', suffix)


    def config(self, key, val):
        self.defs[key] = '"' + val + '"'


    def config_int(self, key, val):
        self.defs[key] = str(val)


    def require(self, lib='', program=None):
        if not program:
            program = self.pkg_config

        env = self.env
        path = run_cmd('which %s' % (program))
        if len(path) is 0:
            stderr.write('required program "%s" not found.\n' % program)
            exit(1)

        # single-quote stuff for pkg-config
        if len(lib) > 0:
            lib = "'%s'" % lib

        if len(lib) > 0:
            # assumes a pkg-config-like program
            ret = run_cmd('%s --exists %s' % (program, lib), 'returncode')
            if ret != 0:
                stderr.write('required library %s not found.\n' % lib)
                exit(1)

        for info in ['cflags', 'libs', 'ldflags']:
            if info in env:
                existing = env[info]
            else:
                existing = ''
            new = run_cmd('%s --%s %s' % (program, info, lib)).strip()
            env[info] = existing + ' ' + new

    def generate(self, mk_config='config.mk', header_config='src/config.h'):
        if self.gcov_build or self.clangcov_build:
            self.append('cflags', '-D_PROF')

        with open(mk_config, 'w') as config:
            for info in sorted(self.env.keys()):
                values = self.env[info].strip()
                config.write('%s := %s\n' % (info.upper(),
                                             values))

        with open(header_config, 'w') as config:
            config.write('// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-\n')
            config.write('// auto-generated by ./configure, do not edit.\n\n')
            config.write('#pragma once\n')
            config.write('#ifndef MESH__CONFIG_H\n')
            config.write('#define MESH__CONFIG_H\n\n')
            for var in sorted(self.defs.keys()):
                value = self.defs[var].strip()
                config.write('#define %s %s\n' %
                             (var.upper().replace('-', '_'), value))
            config.write('\n#endif // MESH__CONFIG_H\n')

        src_dir = dirname(argv[0])
        if not samefile(src_dir, getcwd()):
            copyfile(join(src_dir, 'Makefile'), 'Makefile')

    def append(self, var, val, sep=' '):
        self.env[var] = self.env.get(var, '') + sep + val

    def prefer(self, cmd, preferred):
        if exe_available(preferred):
            self.env[cmd] = preferred
        else:
            stderr.write('warning: %s not found, using default %s\n' % (preferred, cmd))
