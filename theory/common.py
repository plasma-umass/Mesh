# -*- coding: utf-8 -*-

import sys

__all__ = ['ERROR', 'WARN', 'INFO', 'DEBUG', 'log', 'slurp']

# from rainbow
def make_reporter(verbosity, quiet, filelike):
    '''
    Returns a function suitable for logging use.
    '''
    if not quiet:

        def report(level, msg, *args):
            'Log if the specified severity is <= the initial verbosity.'
            if level <= verbosity:
                if len(args):
                    filelike.write(msg % args + '\n')
                else:
                    filelike.write('%s\n' % (msg, ))
    else:

        def report(level, msg, *args):
            '/dev/null logger.'
            pass

    return report


ERROR = 0
WARN = 1
INFO = 2
DEBUG = 3

log = make_reporter(DEBUG, False, sys.stderr)

def slurp(file_name):
    '''
    Reads in a file, stripping leading and trailing whitespace.
    '''
    with open(file_name, 'r') as f:
        return f.read().strip()
