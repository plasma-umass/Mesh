#!/usr/bin/env python3

# Copyright (c) 2013, 2014 Austin Clements

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

import sys
import os
import errno
import argparse
import shlex
import json
import subprocess
import re
import collections
import hashlib
import shutil
import curses
import filecmp
import io
import traceback
import time

try:
    import fcntl
except ImportError:
    # Non-UNIX platform
    fcntl = None

def debug(string, *args):
    if debug.enabled:
        print(string.format(*args), file=sys.stderr)
debug.enabled = False

def debug_exc():
    if debug.enabled:
        traceback.print_exc()

def main():
    # Parse command-line
    arg_parser = argparse.ArgumentParser(
        description='''A 21st century LaTeX wrapper,
        %(prog)s runs latex (and bibtex) the right number of times so you
        don't have to,
        strips the log spew to make errors visible,
        and plays well with standard build tools.''')
    arg_parser.add_argument(
        '-o', metavar='FILE', dest='output', default=None,
        help='Output file name (default: derived from input file)')
    arg_parser.add_argument(
        '--latex-cmd', metavar='CMD', default='pdflatex',
        help='Latex command (default: %(default)s)')
    arg_parser.add_argument(
        '--latex-args', metavar='ARGS', type=arg_parser_shlex,
        help='Additional command-line arguments for latex.'
        ' This will be parsed and split using POSIX shell rules.')
    arg_parser.add_argument(
        '--bibtex-cmd', metavar='CMD', default='bibtex',
        help='Bibtex command (default: %(default)s)')
    arg_parser.add_argument(
        '--bibtex-args', metavar='ARGS', type=arg_parser_shlex,
        help='Additional command-line arguments for bibtex')
    arg_parser.add_argument(
        '--max-iterations', metavar='N', type=int, default=10,
        help='Max number of times to run latex before giving up'
        ' (default: %(default)s)')
    arg_parser.add_argument(
        '-W', metavar='(no-)CLASS',
        action=ArgParserWarnAction, dest='nowarns', default=set(['underfull']),
        help='Enable/disable warning from CLASS, which can be any package name, '
        'LaTeX warning class (e.g., font), bad box type '
        '(underfull, overfull, loose, tight), or "all"')
    arg_parser.add_argument(
        '-O', metavar='DIR', dest='obj_dir', default='latex.out',
        help='Directory for intermediate files and control database '
        '(default: %(default)s)')
    arg_parser.add_argument(
        '--color', choices=('auto', 'always', 'never'), default='auto',
        help='When to colorize messages')
    arg_parser.add_argument(
        '--verbose-cmds', action='store_true', default=False,
        help='Print commands as they are executed')
    arg_parser.add_argument(
        '--debug', action='store_true',
        help='Enable detailed debug output')
    actions = arg_parser.add_argument_group('actions')
    actions.add_argument(
        '--clean-all', action='store_true', help='Delete output files')
    actions.add_argument(
        'file', nargs='?', help='.tex file to compile')
    args = arg_parser.parse_args()
    if not any([args.clean_all, args.file]):
        arg_parser.error('at least one action is required')
    args.latex_args = args.latex_args or []
    args.bibtex_args = args.bibtex_args or []

    verbose_cmd.enabled = args.verbose_cmds
    debug.enabled = args.debug

    # A note about encodings: POSIX encoding is a mess; TeX encoding
    # is a disaster.  Our goal is to make things no worse, so we want
    # byte-accurate round-tripping of TeX messages.  Since TeX
    # messages are *basically* text, we use strings and
    # surrogateescape'ing for both input and output.  I'm not fond of
    # setting surrogateescape globally, but it's far easier than
    # dealing with every place we pass TeX output through.
    # Conveniently, JSON can round-trip surrogateescape'd strings, so
    # our control database doesn't need special handling.
    sys.stdout = io.TextIOWrapper(
        sys.stdout.buffer, encoding=sys.stdout.encoding,
        errors='surrogateescape', line_buffering=sys.stdout.line_buffering)
    sys.stderr = io.TextIOWrapper(
        sys.stderr.buffer, encoding=sys.stderr.encoding,
        errors='surrogateescape', line_buffering=sys.stderr.line_buffering)

    Message.setup_color(args.color)

    # Open control database.
    dbpath = os.path.join(args.obj_dir, '.latexrun.db')
    if not os.path.exists(dbpath) and os.path.exists('.latexrun.db'):
        # The control database used to live in the source directory.
        # Support this for backwards compatibility.
        dbpath = '.latexrun.db'
    try:
        db = DB(dbpath)
    except (ValueError, OSError) as e:
        print('error opening {}: {}'.format(e.filename if hasattr(e, 'filename')
                                            else dbpath, e),
              file=sys.stderr)
        debug_exc()
        sys.exit(1)

    # Clean
    if args.clean_all:
        try:
            db.do_clean(args.obj_dir)
        except OSError as e:
            print(e, file=sys.stderr)
            debug_exc()
            sys.exit(1)

    # Build
    if not args.file:
        return
    task_commit = None
    try:
        task_latex = LaTeX(db, args.file, args.latex_cmd, args.latex_args,
                           args.obj_dir, args.nowarns)
        task_commit = LaTeXCommit(db, task_latex, args.output)
        task_bibtex = BibTeX(db, task_latex, args.bibtex_cmd, args.bibtex_args,
                             args.nowarns, args.obj_dir)
        tasks = [task_latex, task_commit, task_bibtex]
        stable = run_tasks(tasks, args.max_iterations)

        # Print final task output and gather exit status
        status = 0
        for task in tasks:
            status = max(task.report(), status)

        if not stable:
            print('error: files are still changing after {} iterations; giving up'
                  .format(args.max_iterations), file=sys.stderr)
            status = max(status, 1)
    except TaskError as e:
        print(str(e), file=sys.stderr)
        debug_exc()
        status = 1

    # Report final status, if interesting
    fstatus = 'There were errors' if task_commit is None else task_commit.status
    if fstatus:
        output = args.output
        if output is None:
            if task_latex.get_outname() is not None:
                output = os.path.basename(task_latex.get_outname())
            else:
                output = 'output'
        if Message._color:
            terminfo.send('bold', ('setaf', 1))
        print('{}; {} not updated'.format(fstatus, output))
        if Message._color:
            terminfo.send('sgr0')
    sys.exit(status)

def arg_parser_shlex(string):
    """Argument parser for shell token lists."""
    try:
        return shlex.split(string)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e)) from None

class ArgParserWarnAction(argparse.Action):
    def __call__(self, parser, namespace, value, option_string=None):
        nowarn = getattr(namespace, self.dest)
        if value == 'all':
            nowarn.clear()
        elif value.startswith('no-'):
            nowarn.add(value[3:])
        else:
            nowarn.discard(value)
        setattr(namespace, self.dest, nowarn)

def verbose_cmd(args, cwd=None, env=None):
    if verbose_cmd.enabled:
        cmd = ' '.join(map(shlex.quote, args))
        if cwd is not None:
            cmd = '(cd {} && {})'.format(shlex.quote(cwd), cmd)
        if env is not None:
            for k, v in env.items():
                if os.environ.get(k) != v:
                    cmd = '{}={} {}'.format(k, shlex.quote(v), cmd)
        print(cmd, file=sys.stderr)
verbose_cmd.enabled = False

def mkdir_p(path):
    try:
        os.makedirs(path)
    except OSError as exc:
        if exc.errno == errno.EEXIST and os.path.isdir(path):
            pass
        else: raise

class DB:
    """A latexrun control database."""

    _VERSION = 'latexrun-db-v2'

    def __init__(self, filename):
        self.__filename = filename

        # Make sure database directory exists
        if os.path.dirname(self.__filename):
            os.makedirs(os.path.dirname(self.__filename), exist_ok=True)

        # Lock the database if possible. We don't release this lock
        # until the process exits.
        lockpath = self.__filename + '.lock'
        if fcntl is not None:
            lockfd = os.open(lockpath, os.O_CREAT|os.O_WRONLY|os.O_CLOEXEC, 0o666)
            # Note that this is actually an fcntl lock, not a lockf
            # lock. Don't be fooled.
            fcntl.lockf(lockfd, fcntl.LOCK_EX, 1)

        try:
            fp = open(filename, 'r')
        except FileNotFoundError:
            debug('creating new database')
            self.__val = {'version': DB._VERSION}
        else:
            debug('loading database')
            self.__val = json.load(fp)
            if 'version' not in self.__val:
                raise ValueError('file exists, but does not appear to be a latexrun database'.format(filename))
            if self.__val['version'] != DB._VERSION:
                raise ValueError('unknown database version {!r}'
                                 .format(self.__val['version']))

    def commit(self):
        debug('committing database')
        # Atomically commit database
        tmp_filename = self.__filename + '.tmp'
        with open(tmp_filename, 'w') as fp:
            json.dump(self.__val, fp, indent=2, separators=(',', ': '))
            fp.flush()
            os.fsync(fp.fileno())
        os.rename(tmp_filename, self.__filename)

    def get_summary(self, task_id):
        """Return the recorded summary for the given task or None."""
        return self.__val.get('tasks', {}).get(task_id)

    def set_summary(self, task_id, summary):
        """Set the summary for the given task."""
        self.__val.setdefault('tasks', {})[task_id] = summary

    def add_clean(self, filename):
        """Add an output file to be cleaned.

        Unlike the output files recorded in the task summaries,
        cleanable files strictly accumulate until a clean is
        performed.
        """
        self.__val.setdefault('clean', {})[filename] = hash_cache.get(filename)

    def do_clean(self, obj_dir=None):
        """Remove output files and delete database.

        If obj_dir is not None and it is empty after all files are
        removed, it will also be removed.
        """

        for f, want_hash in self.__val.get('clean', {}).items():
            have_hash = hash_cache.get(f)
            if have_hash is not None:
                if want_hash == have_hash:
                    debug('unlinking {}', f)
                    hash_cache.invalidate(f)
                    os.unlink(f)
                else:
                    print('warning: {} has changed; not removing'.format(f),
                          file=sys.stderr)
        self.__val = {'version': DB._VERSION}
        try:
            os.unlink(self.__filename)
        except FileNotFoundError:
            pass
        if obj_dir is not None:
            try:
                os.rmdir(obj_dir)
            except OSError:
                pass

class HashCache:
    """Cache of file hashes.

    As latexrun reaches fixed-point, it hashes the same files over and
    over, many of which never change.  Since hashing is somewhat
    expensive, we keep a simple cache of these hashes.
    """

    def __init__(self):
        self.__cache = {}

    def get(self, filename):
        """Return the hash of filename, or * if it was clobbered."""
        try:
            with open(filename, 'rb') as fp:
                st = os.fstat(fp.fileno())
                key = (st.st_dev, st.st_ino)
                if key in self.__cache:
                    return self.__cache[key]

                debug('hashing {}', filename)
                h = hashlib.sha256()
                while True:
                    block = fp.read(256*1024)
                    if not len(block):
                        break
                    h.update(block)
                self.__cache[key] = h.hexdigest()
                return self.__cache[key]
        except (FileNotFoundError, IsADirectoryError):
            return None

    def clobber(self, filename):
        """If filename's hash is not known, record an invalid hash.

        This can be used when filename was overwritten before we were
        necessarily able to obtain its hash.  filename must exist.
        """
        st = os.stat(filename)
        key = (st.st_dev, st.st_ino)
        if key not in self.__cache:
            self.__cache[key] = '*'

    def invalidate(self, filename):
        try:
            st = os.stat(filename)
        except OSError as e:
            # Pessimistically wipe the whole cache
            debug('wiping hash cache ({})', e)
            self.__cache.clear()
        else:
            key = (st.st_dev, st.st_ino)
            if key in self.__cache:
                del self.__cache[key]
hash_cache = HashCache()

class _Terminfo:
    def __init__(self):
        self.__tty = os.isatty(sys.stdout.fileno())
        if self.__tty:
            curses.setupterm()
        self.__ti = {}

    def __ensure(self, cap):
        if cap not in self.__ti:
            if not self.__tty:
                string = None
            else:
                string = curses.tigetstr(cap)
                if string is None or b'$<' in string:
                    # Don't have this capability or it has a pause
                    string = None
            self.__ti[cap] = string
        return self.__ti[cap]

    def has(self, *caps):
        return all(self.__ensure(cap) is not None for cap in caps)

    def send(self, *caps):
        # Flush TextIOWrapper to the binary IO buffer
        sys.stdout.flush()
        for cap in caps:
            # We should use curses.putp here, but it's broken in
            # Python3 because it writes directly to C's buffered
            # stdout and there's no way to flush that.
            if isinstance(cap, tuple):
                s = curses.tparm(self.__ensure(cap[0]), *cap[1:])
            else:
                s = self.__ensure(cap)
            sys.stdout.buffer.write(s)
terminfo = _Terminfo()

class Progress:
    _enabled = None

    def __init__(self, prefix):
        self.__prefix = prefix
        if Progress._enabled is None:
            Progress._enabled = (not debug.enabled) and \
                                terminfo.has('cr', 'el', 'rmam', 'smam')

    def __enter__(self):
        self.last = ''
        self.update('')
        return self

    def __exit__(self, typ, value, traceback):
        if Progress._enabled:
            # Beginning of line and clear
            terminfo.send('cr', 'el')
            sys.stdout.flush()

    def update(self, msg):
        if not Progress._enabled:
            return
        out = '[' + self.__prefix + ']'
        if msg:
            out += ' ' + msg
        if out != self.last:
            # Beginning of line, clear line, disable wrap
            terminfo.send('cr', 'el', 'rmam')
            sys.stdout.write(out)
            # Enable wrap
            terminfo.send('smam')
            self.last = out
            sys.stdout.flush()

class Message(collections.namedtuple(
        'Message', 'typ filename lineno msg')):
    def emit(self):
        if self.filename:
            if self.filename.startswith('./'):
                finfo = self.filename[2:]
            else:
                finfo = self.filename
        else:
            finfo = '<no file>'
        if self.lineno is not None:
            finfo += ':' + str(self.lineno)
        finfo += ': '
        if self._color:
            terminfo.send('bold')
        sys.stdout.write(finfo)

        if self.typ != 'info':
            if self._color:
                terminfo.send(('setaf', 5 if self.typ == 'warning' else 1))
            sys.stdout.write(self.typ + ': ')
        if self._color:
            terminfo.send('sgr0')
        sys.stdout.write(self.msg + '\n')

    @classmethod
    def setup_color(cls, state):
        if state == 'never':
            cls._color = False
        elif state == 'always':
            cls._color = True
        elif state == 'auto':
            cls._color = terminfo.has('setaf', 'bold', 'sgr0')
        else:
            raise ValueError('Illegal color state {:r}'.format(state))


##################################################################
# Task framework
#

terminate_task_loop = False
start_time = time.time()

def run_tasks(tasks, max_iterations):
    """Execute tasks in round-robin order until all are stable.

    This will also exit if terminate_task_loop is true.  Tasks may use
    this to terminate after a fatal error (even if that fatal error
    doesn't necessarily indicate stability; as long as re-running the
    task will never eliminate the fatal error).

    Return True if fixed-point is reached or terminate_task_loop is
    set within max_iterations iterations.
    """

    global terminate_task_loop
    terminate_task_loop = False

    nstable = 0
    for iteration in range(max_iterations):
        for task in tasks:
            if task.stable():
                nstable += 1
                if nstable == len(tasks):
                    debug('fixed-point reached')
                    return True
            else:
                task.run()
                nstable = 0
                if terminate_task_loop:
                    debug('terminate_task_loop set')
                    return True
    debug('fixed-point not reached')
    return False

class TaskError(Exception):
    pass

class Task:
    """A deterministic computation whose inputs and outputs can be captured."""

    def __init__(self, db, task_id):
        self.__db = db
        self.__task_id = task_id

    def __debug(self, string, *args):
        if debug.enabled:
            debug('task {}: {}', self.__task_id, string.format(*args))

    def stable(self):
        """Return True if running this task will not affect system state.

        Functionally, let f be the task, and s be the system state.
        Then s' = f(s).  If it must be that s' == s (that is, f has
        reached a fixed point), then this function must return True.
        """
        last_summary = self.__db.get_summary(self.__task_id)
        if last_summary is None:
            # Task has never run, so running it will modify system
            # state
            changed = 'never run'
        else:
            # If any of the inputs have changed since the last run of
            # this task, the result may change, so re-run the task.
            # Also, it's possible something else changed an output
            # file, in which case we also want to re-run the task, so
            # check the outputs, too.
            changed = self.__summary_changed(last_summary)

        if changed:
            self.__debug('unstable (changed: {})', changed)
            return False
        else:
            self.__debug('stable')
            return True

    def __summary_changed(self, summary):
        """Test if any inputs changed from summary.

        Returns a string describing the changed input, or None.
        """
        for dep in summary['deps']:
            fn, args, val = dep
            method = getattr(self, '_input_' + fn, None)
            if method is None:
                return 'unknown dependency method {}'.format(fn)
            if method == self._input_unstable or method(*args) != val:
                return '{}{}'.format(fn, tuple(args))
        return None

    def _input(self, name, *args):
        """Register an input for this run.

        This calls self._input_<name>(*args) to get the value of this
        input.  This function should run quickly and return some
        projection of system state that affects the result of this
        computation.

        Both args and the return value must be JSON serializable.
        """
        method = getattr(self, '_input_' + name)
        val = method(*args)
        if [name, args, val] not in self.__deps:
            self.__deps.append([name, args, val])
        return val

    def run(self):
        # Before we run the task, pre-hash any files that were output
        # files in the last run.  These may be input by this run and
        # then clobbered, at which point it will be too late to get an
        # input hash.  Ideally we would only hash files that were
        # *both* input and output files, but latex doesn't tell us
        # about input files that didn't exist, so if we start from a
        # clean slate, we often require an extra run because we don't
        # know a file is input/output until after the second run.
        last_summary = self.__db.get_summary(self.__task_id)
        if last_summary is not None:
            for io_filename in last_summary['output_files']:
                self.__debug('pre-hashing {}', io_filename)
                hash_cache.get(io_filename)

        # Run the task
        self.__debug('running')
        self.__deps = []
        result = self._execute()

        # Clear cached output file hashes
        for filename in result.output_filenames:
            hash_cache.invalidate(filename)

        # If the output files change, then the computation needs to be
        # re-run, so record them as inputs
        for filename in result.output_filenames:
            self._input('file', filename)

        # Update task summary in database
        self.__db.set_summary(self.__task_id,
                              self.__make_summary(self.__deps, result))
        del self.__deps

        # Add output files to be cleaned
        for f in result.output_filenames:
            self.__db.add_clean(f)

        try:
            self.__db.commit()
        except OSError as e:
            raise TaskError('error committing control database {}: {}'.format(
                getattr(e, 'filename', '<unknown path>'), e)) from e

    def __make_summary(self, deps, run_result):
        """Construct a new task summary."""
        return {
            'deps': deps,
            'output_files': {f: hash_cache.get(f)
                             for f in run_result.output_filenames},
            'extra': run_result.extra,
        }

    def _execute(self):
        """Abstract: Execute this task.

        Subclasses should implement this method to execute this task.
        This method must return a RunResult giving the inputs that
        were used by the task and the outputs it produced.
        """
        raise NotImplementedError('Task._execute is abstract')

    def _get_result_extra(self):
        """Return the 'extra' result from the previous run, or None."""
        summary = self.__db.get_summary(self.__task_id)
        if summary is None:
            return None
        return summary['extra']

    def report(self):
        """Report the task's results to stdout and return exit status.

        This may be called when the task has never executed.
        Subclasses should override this.  The default implementation
        reports nothing and returns 0.
        """
        return 0

    # Standard input functions

    def _input_env(self, var):
        return os.environ.get(var)

    def _input_file(self, path):
        return hash_cache.get(path)

    def _input_unstable(self):
        """Mark this run as unstable, regardless of other inputs."""
        return None

    def _input_unknown_input(self):
        """An unknown input that may change after latexrun exits.

        This conservatively marks some unknown input that definitely
        won't change while latexrun is running, but may change before
        the user next runs latexrun.  This allows the task to
        stabilize during this invocation, but will cause the task to
        re-run on the next invocation.
        """
        return start_time

class RunResult(collections.namedtuple(
        'RunResult', 'output_filenames extra')):
    """The result of a single task execution.

    This captures all files written by the task, and task-specific
    results that need to be persisted between runs (for example, to
    enable reporting of a task's results).
    """
    pass

##################################################################
# LaTeX task
#

def normalize_input_path(path):
    # Resolve the directory of the input path, but leave the file
    # component alone because it affects TeX's behavior.
    head, tail = os.path.split(path)
    npath = os.path.join(os.path.realpath(head), tail)
    return os.path.relpath(path)

class LaTeX(Task):
    def __init__(self, db, tex_filename, cmd, cmd_args, obj_dir, nowarns):
        super().__init__(db, 'latex::' + normalize_input_path(tex_filename))
        self.__tex_filename = tex_filename
        self.__cmd = cmd
        self.__cmd_args = cmd_args
        self.__obj_dir = obj_dir
        self.__nowarns = nowarns

        self.__pass = 0

    def _input_args(self):
        # If filename starts with a character the tex command-line
        # treats specially, then tweak it so it doesn't.
        filename = self.__tex_filename
        if filename.startswith(('-', '&', '\\')):
            filename = './' + filename
        # XXX Put these at the beginning in case the provided
        # arguments are malformed.  Might want to do a best-effort
        # check for incompatible user-provided arguments (note:
        # arguments can be given with one or two dashes and those with
        # values can use an equals or a space).
        return [self.__cmd] + self.__cmd_args + \
            ['-interaction', 'nonstopmode', '-recorder',
             '-output-directory', self.__obj_dir, filename]

    def _execute(self):
        # Run latex
        self.__pass += 1
        args = self._input('args')
        debug('running {}', args)
        try:
            os.makedirs(self.__obj_dir, exist_ok=True)
        except OSError as e:
            raise TaskError('failed to create %s: ' % self.__obj_dir + str(e)) \
                from e
        try:
            verbose_cmd(args)
            p = subprocess.Popen(args,
                                 stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
            stdout, has_errors, missing_includes = self.__feed_terminal(p.stdout)
            status = p.wait()
        except OSError as e:
            raise TaskError('failed to execute latex task: ' + str(e)) from e

        # Register environment variable inputs
        for env_var in ['TEXMFOUTPUT', 'TEXINPUTS', 'TEXFORMATS', 'TEXPOOL',
                        'TFMFONTS', 'PATH']:
            self._input('env', env_var)

        jobname, outname = self.__parse_jobname(stdout)
        inputs, outputs = self.__parse_recorder(jobname)

        # LaTeX overwrites its own inputs.  Mark its output files as
        # clobbered before we hash its input files.
        for path in outputs:
            # In some abort cases (e.g., >=100 errors), LaTeX claims
            # output files that don't actually exist.
            if os.path.exists(path):
                hash_cache.clobber(path)
        # Depend on input files.  Task.run pre-hashed outputs from the
        # previous run, so if this isn't the first run and as long as
        # the set of outputs didn't change, we'll be able to get the
        # input hashes, even if they were clobbered.
        for path in inputs:
            self._input('file', path)

        if missing_includes:
            # Missing \includes are tricky.  Ideally we'd depend on
            # the absence of some file, but in fact we'd have to
            # depend on the failure of a whole kpathsea lookup.
            # Rather than try to be clever, just mark this as an
            # unknown input so we'll run at least once on the next
            # invocation.
            self._input('unknown_input')

        if not self.__create_outdirs(stdout) and has_errors:
            # LaTeX reported unrecoverable errors (other than output
            # directory errors, which we just fixed).  We could
            # continue to stabilize the document, which may change
            # some of the other problems reported (but not the
            # unrecoverable errors), or we can just abort now and get
            # back to the user quickly with the major errors.  We opt
            # for the latter.
            global terminate_task_loop
            terminate_task_loop = True
            # This error could depend on something we failed to track.
            # It would be really confusing if we continued to report
            # the error after the user fixed it, so be conservative
            # and force a re-run next time.
            self._input('unknown_input')

        return RunResult(outputs,
                         {'jobname': jobname, 'outname': outname,
                          'status': status})

    def __feed_terminal(self, stdout):
        prefix = 'latex'
        if self.__pass > 1:
            prefix += ' ({})'.format(self.__pass)
        with Progress(prefix) as progress:
            buf = []
            filt = LaTeXFilter()
            while True:
                # Use os.read to read only what's available on the pipe,
                # without waiting to fill a buffer
                data = os.read(stdout.fileno(), 4096)
                if not data:
                    break
                # See "A note about encoding" above
                data = data.decode('ascii', errors='surrogateescape')
                buf.append(data)
                filt.feed(data)
                file_stack = filt.get_file_stack()
                if file_stack:
                    tos = file_stack[-1]
                    if tos.startswith('./'):
                        tos = tos[2:]
                    progress.update('>' * len(file_stack) + ' ' + tos)
                else:
                    progress.update('')

            # Were there unrecoverable errors?
            has_errors = any(msg.typ == 'error' for msg in filt.get_messages())

            return ''.join(buf), has_errors, filt.has_missing_includes()

    def __parse_jobname(self, stdout):
        """Extract the job name and output name from latex's output.

        We get these from latex because they depend on complicated
        file name parsing rules, are affected by arguments like
        -output-directory, and may be just "texput" if things fail
        really early.  The output name may be None if there were no
        pages of output.
        """
        jobname = outname = None
        for m in re.finditer(r'^Transcript written on "?(.*)\.log"?\.$', stdout,
                             re.MULTILINE | re.DOTALL):
            jobname = m.group(1).replace('\n', '')
        if jobname is None:
            print(stdout, file=sys.stderr)
            raise TaskError('failed to extract job name from latex log')
        for m in re.finditer(r'^Output written on "?(.*\.[^ ."]+)"? \([0-9]+ page',
                             stdout, re.MULTILINE | re.DOTALL):
            outname = m.group(1).replace('\n', '')
        if outname is None and not \
           re.search(r'^No pages of output\.$|^! Emergency stop\.$'
                     r'|^!  ==> Fatal error occurred, no output PDF file produced!$',
                     stdout, re.MULTILINE):
            print(stdout, file=sys.stderr)
            raise TaskError('failed to extract output name from latex log')

        # LuaTeX (0.76.0) doesn't include the output directory in the
        # logged transcript or output file name.
        if os.path.basename(jobname) == jobname and \
           os.path.exists(os.path.join(self.__obj_dir, jobname + '.log')):
            jobname = os.path.join(self.__obj_dir, jobname)
            if outname is not None:
                outname = os.path.join(self.__obj_dir, outname)

        return jobname, outname

    def __parse_recorder(self, jobname):
        """Parse file recorder output."""
        # XXX If latex fails because a file isn't found, that doesn't
        # go into the .fls file, but creating that file will affect
        # the computation, so it should be included as an input.
        # Though it's generally true that files can be added earlier
        # in search paths and will affect the output without us knowing.
        #
        # XXX This is a serious problem for bibtex, since the first
        # run won't depend on the .bbl file!  But maybe the .aux file
        # will always cause a re-run, at which point the .bbl will
        # exist?
        filename = jobname + '.fls'
        try:
            recorder = open(filename)
        except OSError as e:
            raise TaskError('failed to open file recorder output: ' + str(e)) \
                from e
        pwd, inputs, outputs = '', set(), set()
        for linenum, line in enumerate(recorder):
            parts = line.rstrip('\n').split(' ', 1)
            if parts[0] == 'PWD':
                pwd = parts[1]
            elif parts[0] in ('INPUT', 'OUTPUT'):
                if parts[1].startswith('/'):
                    path = parts[1]
                else:
                    # Try to make "nice" paths, especially for clean
                    path = os.path.relpath(os.path.join(pwd, parts[1]))
                if parts[0] == 'INPUT':
                    inputs.add(path)
                else:
                    outputs.add(path)
            else:
                raise TaskError('syntax error on line {} of {}'
                                .format(linenum, filename))
        # Ironically, latex omits the .fls file itself
        outputs.add(filename)
        return inputs, outputs

    def __create_outdirs(self, stdout):
        # In some cases, such as \include'ing a file from a
        # subdirectory, TeX will attempt to create files in
        # subdirectories of the output directory that don't exist.
        # Detect this, create the output directory, and re-run.
        m = re.search('^! I can\'t write on file `(.*)\'\\.$', stdout, re.M)
        if m and m.group(1).find('/') > 0 and '../' not in m.group(1):
            debug('considering creating output sub-directory for {}'.
                  format(m.group(1)))
            subdir = os.path.dirname(m.group(1))
            newdir = os.path.join(self.__obj_dir, subdir)
            if os.path.isdir(subdir) and not os.path.isdir(newdir):
                debug('creating output subdirectory {}'.format(newdir))
                try:
                    mkdir_p(newdir)
                except OSError as e:
                    raise TaskError('failed to create output subdirectory: ' +
                                    str(e)) from e
                self._input('unstable')
                return True

    def report(self):
        extra = self._get_result_extra()
        if extra is None:
            return 0

        # Parse the log
        logfile = open(extra['jobname'] + '.log', 'rt', errors='surrogateescape')
        for msg in self.__clean_messages(
                LaTeXFilter(self.__nowarns).feed(
                    logfile.read(), True).get_messages()):
            msg.emit()

        # Return LaTeX's exit status
        return extra['status']

    def __clean_messages(self, msgs):
        """Make some standard log messages more user-friendly."""
        have_undefined_reference = False
        for msg in msgs:
            if msg.msg == '==> Fatal error occurred, no output PDF file produced!':
                msg = msg._replace(typ='info',
                                   msg='Fatal error (no output file produced)')
            if msg.msg.startswith('[LaTeX] '):
                # Strip unnecessary package name
                msg = msg._replace(msg=msg.msg.split(' ', 1)[1])
            if re.match(r'Reference .* undefined', msg.msg):
                have_undefined_reference = True
            if have_undefined_reference and \
               re.match(r'There were undefined references', msg.msg):
                # LaTeX prints this at the end so the user knows it's
                # worthwhile looking back at the log.  Since latexrun
                # makes the earlier messages obvious, this is
                # redundant.
                continue
            yield msg

    def get_tex_filename(self):
        return self.__tex_filename

    def get_jobname(self):
        extra = self._get_result_extra()
        if extra is None:
            return None
        return extra['jobname']

    def get_outname(self):
        extra = self._get_result_extra()
        if extra is None:
            return None
        return extra['outname']

    def get_status(self):
        extra = self._get_result_extra()
        if extra is None:
            return None
        return extra['status']

class LaTeXCommit(Task):
    def __init__(self, db, latex_task, output_path):
        super().__init__(db, 'latex_commit::' +
                         normalize_input_path(latex_task.get_tex_filename()))
        self.__latex_task = latex_task
        self.__output_path = output_path
        self.status = 'There were errors'

    def _input_latex(self):
        return self.__latex_task.get_status(), self.__latex_task.get_outname()

    def _execute(self):
        self.status = 'There were errors'

        # If latex succeeded with output, atomically commit the output
        status, outname = self._input('latex')
        if status != 0 or outname is None:
            debug('not committing (status {}, outname {})', status, outname)
            if outname is None:
                self.status = 'No pages of output'
            return RunResult([], None)

        commit = self.__output_path or os.path.basename(outname)
        if os.path.abspath(commit) == os.path.abspath(outname):
            debug('skipping commit (outname is commit name)')
            self.status = None
            return RunResult([], None)

        try:
            if os.path.exists(commit) and filecmp.cmp(outname, commit):
                debug('skipping commit ({} and {} are identical)',
                      outname, commit)
                # To avoid confusion, touch the output file
                open(outname, 'r+b').close()
            else:
                debug('commiting {} to {}', outname, commit)
                shutil.copy(outname, outname + '~')
                os.rename(outname + '~', commit)
        except OSError as e:
            raise TaskError('error committing latex output: {}'.format(e)) from e
        self._input('file', outname)
        self.status = None
        return RunResult([commit], None)

class LaTeXFilter:
    TRACE = False               # Set to enable detailed parse tracing

    def __init__(self, nowarns=[]):
        self.__data = ''
        self.__restart_pos = 0
        self.__restart_file_stack = []
        self.__restart_messages_len = 0
        self.__messages = []
        self.__first_file = None
        self.__fatal_error = False
        self.__missing_includes = False
        self.__pageno = 1
        self.__restart_pageno = 1

        self.__suppress = {cls: 0 for cls in nowarns}

    def feed(self, data, eof=False):
        """Feed LaTeX log data to the parser.

        The log data can be from LaTeX's standard output, or from the
        log file.  If there will be no more data, set eof to True.
        """

        self.__data += data
        self.__data_complete = eof

        # Reset to last known-good restart point
        self.__pos = self.__restart_pos
        self.__file_stack = self.__restart_file_stack.copy()
        self.__messages = self.__messages[:self.__restart_messages_len]
        self.__lstart = self.__lend = -1
        self.__pageno = self.__restart_pageno

        # Parse forward
        while self.__pos < len(self.__data):
            self.__noise()

        # Handle suppressed warnings
        if eof:
            msgs = ['%d %s warning%s' % (count, cls, "s" if count > 1 else "")
                    for cls, count in self.__suppress.items() if count]
            if msgs:
                self.__message('info', None,
                               '%s not shown (use -Wall to show them)' %
                               ', '.join(msgs), filename=self.__first_file)

        if eof and len(self.__file_stack) and not self.__fatal_error:
            # Fatal errors generally cause TeX to "succumb" without
            # closing the file stack, so don't complain in that case.
            self.__message('warning', None,
                           "unbalanced `(' in log; file names may be wrong")
        return self

    def get_messages(self):
        """Return a list of warning and error Messages."""
        return self.__messages

    def get_file_stack(self):
        """Return the file stack for the data that has been parsed.

        This results a list from outermost file to innermost file.
        The list may be empty.
        """

        return self.__file_stack

    def has_missing_includes(self):
        """Return True if the log reported missing \\include files."""
        return self.__missing_includes

    def __save_restart_point(self):
        """Save the current state as a known-good restart point.

        On the next call to feed, the parser will reset to this point.
        """
        self.__restart_pos = self.__pos
        self.__restart_file_stack = self.__file_stack.copy()
        self.__restart_messages_len = len(self.__messages)
        self.__restart_pageno = self.__pageno

    def __message(self, typ, lineno, msg, cls=None, filename=None):
        if cls is not None and cls in self.__suppress:
            self.__suppress[cls] += 1
            return
        filename = filename or (self.__file_stack[-1] if self.__file_stack
                                else self.__first_file)
        self.__messages.append(Message(typ, filename, lineno, msg))

    def __ensure_line(self):
        """Update lstart and lend."""
        if self.__lstart <= self.__pos < self.__lend:
            return
        self.__lstart = self.__data.rfind('\n', 0, self.__pos) + 1
        self.__lend = self.__data.find('\n', self.__pos) + 1
        if self.__lend == 0:
            self.__lend = len(self.__data)

    @property
    def __col(self):
        """The 0-based column number of __pos."""
        self.__ensure_line()
        return self.__pos - self.__lstart

    @property
    def __avail(self):
        return self.__pos < len(self.__data)

    def __lookingat(self, needle):
        return self.__data.startswith(needle, self.__pos)

    def __lookingatre(self, regexp, flags=0):
        return re.compile(regexp, flags=flags).match(self.__data, self.__pos)

    def __skip_line(self):
        self.__ensure_line()
        self.__pos = self.__lend

    def __consume_line(self, unwrap=False):
        self.__ensure_line()
        data = self.__data[self.__pos:self.__lend]
        self.__pos = self.__lend
        if unwrap:
            # TeX helpfully wraps all terminal output at 79 columns
            # (max_print_line).  If requested, unwrap it.  There's
            # simply no way to do this perfectly, since there could be
            # a line that happens to be 79 columns.
            #
            # We check for >=80 because a bug in LuaTeX causes it to
            # wrap at 80 columns instead of 79 (LuaTeX #900).
            while self.__lend - self.__lstart >= 80:
                if self.TRACE: print('<{}> wrapping'.format(self.__pos))
                self.__ensure_line()
                data = data[:-1] + self.__data[self.__pos:self.__lend]
                self.__pos = self.__lend
        return data

    # Parser productions

    def __noise(self):
        # Most of TeX's output is line noise that combines error
        # messages, warnings, file names, user errors and warnings,
        # and echos of token lists and other input.  This attempts to
        # tease these apart, paying particular attention to all of the
        # places where TeX echos input so that parens in the input do
        # not confuse the file name scanner.  There are three
        # functions in TeX that echo input: show_token_list (used by
        # runaway and show_context, which is used by print_err),
        # short_display (used by overfull/etc h/vbox), and show_print
        # (used in issue_message and the same places as
        # show_token_list).
        lookingat, lookingatre = self.__lookingat, self.__lookingatre
        if self.__col == 0:
            # The following messages are always preceded by a newline
            if lookingat('! '):
                return self.__errmessage()
            if lookingat('!pdfTeX error: '):
                return self.__pdftex_fail()
            if lookingat('Runaway '):
                return self.__runaway()
            if lookingatre(r'(Overfull|Underfull|Loose|Tight) \\[hv]box \('):
                return self.__bad_box()
            if lookingatre('(Package |Class |LaTeX |pdfTeX )?(\w+ )?warning: ', re.I):
                return self.__generic_warning()
            if lookingatre('No file .*\\.tex\\.$', re.M):
                # This happens with \includes of missing files.  For
                # whatever reason, LaTeX doesn't consider this even
                # worth a warning, but I do!
                self.__message('warning', None,
                               self.__simplify_message(
                                   self.__consume_line(unwrap=True).strip()))
                self.__missing_includes = True
                return
            # Other things that are common and irrelevant
            if lookingatre(r'(Package|Class|LaTeX) (\w+ )?info: ', re.I):
                return self.__generic_info()
            if lookingatre(r'(Document Class|File|Package): '):
                # Output from "\ProvidesX"
                return self.__consume_line(unwrap=True)
            if lookingatre(r'\\\w+=\\[a-z]+\d+\n'):
                # Output from "\new{count,dimen,skip,...}"
                return self.__consume_line(unwrap=True)

        # print(self.__data[self.__lstart:self.__lend].rstrip())
        # self.__pos = self.__lend
        # return

        # Now that we've substantially reduced the spew and hopefully
        # eliminated all input echoing, we're left with the file name
        # stack, page outs, and random other messages from both TeX
        # and various packages.  We'll assume at this point that all
        # parentheses belong to the file name stack or, if they're in
        # random other messages, they're at least balanced and nothing
        # interesting happens between them.  For page outs, ship_out
        # prints a space if not at the beginning of a line, then a
        # "[", then the page number being shipped out (this is
        # usually, but not always, followed by "]").
        m = re.compile(r'[(){}\n]|(?<=[\n ])\[\d+', re.M).\
            search(self.__data, self.__pos)
        if m is None:
            self.__pos = len(self.__data)
            return
        self.__pos = m.start() + 1
        ch = self.__data[m.start()]
        if ch == '\n':
            # Save this as a known-good restart point for incremental
            # parsing, since we definitely didn't match any of the
            # known message types above.
            self.__save_restart_point()
        elif ch == '[':
            # This is printed at the end of a page, so we're beginning
            # page n+1.
            self.__pageno = int(self.__lookingatre(r'\d+').group(0)) + 1
        elif ((self.__data.startswith('`', m.start() - 1) or
               self.__data.startswith('`\\', m.start() - 2)) and
               self.__data.startswith('\'', m.start() + 1)):
            # (, ), {, and } sometimes appear in TeX's error
            # descriptions, but they're always in `'s (and sometimes
            # backslashed)
            return
        elif ch == '(':
            # XXX Check that the stack doesn't drop to empty and then re-grow
            first = self.__first_file is None and self.__col == 1
            filename = self.__filename()
            self.__file_stack.append(filename)
            if first:
                self.__first_file = filename
            if self.TRACE:
                print('<{}>{}enter {}'.format(
                    m.start(), ' '*len(self.__file_stack), filename))
        elif ch == ')':
            if len(self.__file_stack):
                if self.TRACE:
                    print('<{}>{}exit {}'.format(
                        m.start(), ' '*len(self.__file_stack),
                        self.__file_stack[-1]))
                self.__file_stack.pop()
            else:
                self.__message('warning', None,
                               "extra `)' in log; file names may be wrong ")
        elif ch == '{':
            # TeX uses this for various things we want to ignore, like
            # file names and print_mark.  Consume up to the '}'
            epos = self.__data.find('}', self.__pos)
            if epos != -1:
                self.__pos = epos + 1
            else:
                self.__message('warning', None,
                               "unbalanced `{' in log; file names may be wrong")
        elif ch == '}':
            self.__message('warning', None,
                           "extra `}' in log; file names may be wrong")

    def __filename(self):
        initcol = self.__col
        first = True
        name = ''
        # File names may wrap, but if they do, TeX will always print a
        # newline before the open paren
        while first or (initcol == 1 and self.__lookingat('\n')
                        and self.__col >= 79):
            if not first:
                self.__pos += 1
            m = self.__lookingatre(r'[^(){} \n]*')
            name += m.group()
            self.__pos = m.end()
            first = False
        return name

    def __simplify_message(self, msg):
        msg = re.sub(r'^(?:Package |Class |LaTeX |pdfTeX )?([^ ]+) (?:Error|Warning): ',
                     r'[\1] ', msg, flags=re.I)
        msg = re.sub(r'\.$', '', msg)
        msg = re.sub(r'has occurred (while \\output is active)', r'\1', msg)
        return msg

    def __errmessage(self):
        # Procedure print_err (including \errmessage, itself used by
        # LaTeX's \GenericError and all of its callers), as well as
        # fatal_error.  Prints "\n!  " followed by error text
        # ("Emergency stop" in the case of fatal_error).  print_err is
        # always followed by a call to error, which prints a period,
        # and a newline...
        msg = self.__consume_line(unwrap=True)[1:].strip()
        is_fatal_error = (msg == 'Emergency stop.')
        msg = self.__simplify_message(msg)
        # ... and then calls show_context, which prints the input
        # stack as pairs of lines giving the context.  These context
        # lines are truncated so they never wrap.  Each pair of lines
        # will start with either "<something> " if the context is a
        # token list, "<*> " for terminal input (or command line),
        # "<read ...>" for stream reads, something like "\macroname
        # #1->" for macros (though everything after \macroname is
        # subject to being elided as "..."), or "l.[0-9]+ " if it's a
        # file.  This is followed by the errant input with a line
        # break where the error occurred.
        lineno = None
        found_context = False
        stack = []
        while self.__avail:
            m1 = self.__lookingatre(r'<([a-z ]+|\*|read [^ >]*)> |\\.*(->|...)')
            m2 = self.__lookingatre('l\.[0-9]+ ')
            if m1:
                found_context = True
                pre = self.__consume_line().rstrip('\n')
                stack.append(pre)
            elif m2:
                found_context = True
                pre = self.__consume_line().rstrip('\n')
                info, rest = pre.split(' ', 1)
                lineno = int(info[2:])
                stack.append(rest)
            elif found_context:
                # Done with context
                break
            if found_context:
                # Consume the second context line
                post = self.__consume_line().rstrip('\n')
                # Clean up goofy trailing ^^M TeX sometimes includes
                post = re.sub(r'\^\^M$', '', post)
                if post[:len(pre)].isspace() and not post.isspace():
                    stack.append(len(stack[-1]))
                    stack[-2] += post[len(pre):]
            else:
                # If we haven't found the context, skip the line.
                self.__skip_line()
        stack_msg = ''
        for i, trace in enumerate(stack):
            stack_msg += ('\n         ' + (' ' * trace) + '^'
                          if isinstance(trace, int) else
                          '\n      at ' + trace.rstrip() if i == 0 else
                          '\n    from ' + trace.rstrip())

        if is_fatal_error:
            # fatal_error always prints one additional line of message
            info = self.__consume_line().strip()
            if info.startswith('*** '):
                info = info[4:]
            msg += ': '  + info.lstrip('(').rstrip(')')

        self.__message('error', lineno, msg + stack_msg)
        self.__fatal_error = True

    def __pdftex_fail(self):
        # Procedure pdftex_fail.  Prints "\n!pdfTeX error: ", the
        # message, and a newline.  Unlike print_err, there's never
        # context.
        msg = self.__consume_line(unwrap=True)[1:].strip()
        msg = self.__simplify_message(msg)
        self.__message('error', None, msg)

    def __runaway(self):
        # Procedure runaway.  Prints "\nRunaway ...\n" possibly
        # followed by token list (user text).  Always followed by a
        # call to print_err, so skip lines until we see the print_err.
        self.__skip_line()      # Skip "Runaway ...\n"
        if not self.__lookingat('! ') and self.__avail:
            # Skip token list, which is limited to one line
            self.__skip_line()

    def __bad_box(self):
        # Function hpack and vpack.  hpack prints a warning, a
        # newline, then a short_display of the offending text.
        # Unfortunately, there's nothing indicating the end of the
        # offending text, but it should be on one (possible wrapped)
        # line.  vpack prints a warning and then, *unless output is
        # active*, a newline.  The missing newline is probably a bug,
        # but it sure makes our lives harder.
        origpos = self.__pos
        msg = self.__consume_line()
        m = re.search(r' in (?:paragraph|alignment) at lines ([0-9]+)--([0-9]+)', msg) or \
            re.search(r' detected at line ([0-9]+)', msg)
        if m:
            # Sometimes TeX prints crazy line ranges like "at lines
            # 8500--250".  The lower number seems roughly sane, so use
            # that.  I'm not sure what causes this, but it may be
            # related to shipout routines messing up line registers.
            lineno = min(int(m.group(1)), int(m.groups()[-1]))
            msg = msg[:m.start()]
        else:
            m = re.search(r' while \\output is active', msg)
            if m:
                lineno = None
                msg = msg[:m.end()]
            else:
                self.__message('warning', None,
                               'malformed bad box message in log')
                return
        # Back up to the end of the known message text
        self.__pos = origpos + m.end()
        if self.__lookingat('\n'):
            # We have a newline, so consume it and look for the
            # offending text.
            self.__pos += 1
            # If there is offending text, it will start with a font
            # name, which will start with a \.
            if 'hbox' in msg and self.__lookingat('\\'):
                self.__consume_line(unwrap=True)
        msg = self.__simplify_message(msg) + ' (page {})'.format(self.__pageno)
        cls = msg.split(None, 1)[0].lower()
        self.__message('warning', lineno, msg, cls=cls)

    def __generic_warning(self):
        # Warnings produced by LaTeX's \GenericWarning (which is
        # called by \{Package,Class}Warning and \@latex@warning),
        # warnings produced by pdftex_warn, and other random warnings.
        msg, cls = self.__generic_info()
        # Most warnings include an input line emitted by \on@line
        m = re.search(' on input line ([0-9]+)', msg)
        if m:
            lineno = int(m.group(1))
            msg = msg[:m.start()]
        else:
            lineno = None
        msg = self.__simplify_message(msg)
        self.__message('warning', lineno, msg, cls=cls)

    def __generic_info(self):
        # Messages produced by LaTeX's \Generic{Error,Warning,Info}
        # and things that look like them
        msg = self.__consume_line(unwrap=True).strip()
        # Package and class messages are continued with lines
        # containing '(package name)            '
        pkg_name = msg.split(' ', 2)[1]
        prefix = '(' + pkg_name + ')            '
        while self.__lookingat(prefix):
            # Collect extra lines.  It's important that we keep these
            # because they may contain context information like line
            # numbers.
            extra = self.__consume_line(unwrap=True)
            msg += ' ' + extra[len(prefix):].strip()
        return msg, pkg_name.lower()

##################################################################
# BibTeX task
#

class BibTeX(Task):
    def __init__(self, db, latex_task, cmd, cmd_args, nowarns, obj_dir):
        super().__init__(db, 'bibtex::' + normalize_input_path(
            latex_task.get_tex_filename()))
        self.__latex_task = latex_task
        self.__cmd = cmd
        self.__cmd_args = cmd_args
        self.__obj_dir = obj_dir

    def stable(self):
        # If bibtex doesn't have its inputs, then it's stable because
        # it has no effect on system state.
        jobname = self.__latex_task.get_jobname()
        if jobname is None:
            # We don't know where the .aux file is until latex has run
            return True
        if not os.path.exists(jobname + '.aux'):
            # Input isn't ready, so bibtex will simply fail without
            # affecting system state.  Hence, this task is trivially
            # stable.
            return True
        if not self.__find_bib_cmds(os.path.dirname(jobname), jobname + '.aux'):
            # The tex file doesn't refer to any bibliographic data, so
            # don't run bibtex.
            return True

        return super().stable()

    def __find_bib_cmds(self, basedir, auxname, stack=()):
        debug('scanning for bib commands in {}'.format(auxname))
        if auxname in stack:
            raise TaskError('.aux file loop')
        stack = stack + (auxname,)

        try:
            aux_data = open(auxname, errors='surrogateescape').read()
        except FileNotFoundError:
            # The aux file may not exist if latex aborted
            return False
        if re.search(r'^\\bibstyle\{', aux_data, flags=re.M) or \
           re.search(r'^\\bibdata\{',  aux_data, flags=re.M):
            return True

        if re.search(r'^\\abx@aux@cite\{', aux_data, flags=re.M):
            # biber citation
            return True

        # Recurse into included aux files (see aux_input_command), in
        # case \bibliography appears in an \included file.
        for m in re.finditer(r'^\\@input\{([^}]*)\}', aux_data, flags=re.M):
            if self.__find_bib_cmds(basedir, os.path.join(basedir, m.group(1)),
                                    stack):
                return True

        return False

    def _input_args(self):
        if self.__is_biber():
            aux_name = os.path.basename(self.__latex_task.get_jobname())
        else:
            aux_name = os.path.basename(self.__latex_task.get_jobname()) + '.aux'
        return [self.__cmd] + self.__cmd_args + [aux_name]

    def _input_cwd(self):
        return os.path.dirname(self.__latex_task.get_jobname())

    def _input_auxfile(self, auxname):
        # We don't consider the .aux files regular inputs.
        # Instead, we extract just the bit that BibTeX cares about
        # and depend on that.  See get_aux_command_and_process in
        # bibtex.web.
        debug('hashing filtered aux file {}', auxname)
        try:
            with open(auxname, 'rb') as aux:
                h = hashlib.sha256()
                for line in aux:
                    if line.startswith((b'\\citation{', b'\\bibdata{',
                                        b'\\bibstyle{', b'\\@input{',
                                        b'\\abx@aux@cite{')):
                        h.update(line)
                return h.hexdigest()
        except FileNotFoundError:
            debug('{} does not exist', auxname)
            return None

    def __path_join(self, first, rest):
        if rest is None:
            # Append ':' to keep the default search path
            return first + ':'
        return first + ':' + rest

    def __is_biber(self):
        return "biber" in self.__cmd

    def _execute(self):
        # This gets complicated when \include is involved.  \include
        # switches to a different aux file and records its path in the
        # main aux file.  However, BibTeX does not consider this path
        # to be relative to the location of the main aux file, so we
        # have to run BibTeX *in the output directory* for it to
        # follow these includes (there's no way to tell BibTeX other
        # locations to search).  Unfortunately, this means BibTeX will
        # no longer be able to find local bib or bst files, but so we
        # tell it where to look by setting BIBINPUTS and BSTINPUTS
        # (luckily we can control this search).  We have to pass this
        # same environment down to Kpathsea when we resolve the paths
        # in BibTeX's log.
        args, cwd = self._input('args'), self._input('cwd')
        debug('running {} in {}', args, cwd)

        env = os.environ.copy()
        env['BIBINPUTS'] = self.__path_join(os.getcwd(), env.get('BIBINPUTS'))
        env['BSTINPUTS'] = self.__path_join(os.getcwd(), env.get('BSTINPUTS'))

        try:
            verbose_cmd(args, cwd, env)
            p = subprocess.Popen(args, cwd=cwd, env=env,
                                 stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
            stdout = self.__feed_terminal(p.stdout)
            status = p.wait()
        except OSError as e:
            raise TaskError('failed to execute bibtex task: ' + str(e)) from e

        inputs, auxnames, outbase = self.__parse_inputs(stdout, cwd, env)
        if not inputs and not auxnames:
            # BibTeX failed catastrophically.
            print(stdout, file=sys.stderr)
            raise TaskError('failed to execute bibtex task')

        # Register environment variable inputs
        for env_var in ['TEXMFOUTPUT', 'BSTINPUTS', 'BIBINPUTS', 'PATH']:
            self._input('env', env_var)

        # Register file inputs
        for path in auxnames:
            self._input('auxfile', path)
        for path in inputs:
            self._input('file', path)

        if self.__is_biber():
            outbase = os.path.join(cwd, outbase)
        outputs = [outbase + '.bbl', outbase + '.blg']
        return RunResult(outputs, {'outbase': outbase, 'status': status,
                                   'inputs': inputs})

    def __feed_terminal(self, stdout):
        with Progress('bibtex') as progress:
            buf, linebuf = [], ''
            while True:
                data = os.read(stdout.fileno(), 4096)
                if not data:
                    break
                # See "A note about encoding" above
                data = data.decode('ascii', errors='surrogateescape')
                buf.append(data)
                linebuf += data
                while '\n' in linebuf:
                    line, _, linebuf = linebuf.partition('\n')
                    if line.startswith('Database file'):
                        progress.update(line.split(': ', 1)[1])
        return ''.join(buf)

    def __parse_inputs(self, log, cwd, env):
        # BibTeX conveniently logs every file that it opens, and its
        # log is actually sensible (see calls to a_open_in in
        # bibtex.web.)  The only trick is that these file names are
        # pre-kpathsea lookup and may be relative to the directory we
        # ran BibTeX in.
        #
        # Because BibTeX actually depends on very little in the .aux
        # file (and it's likely other things will change in the .aux
        # file), we don't count the whole .aux file as an input, but
        # instead depend only on the lines that matter to BibTeX.
        kpathsea = Kpathsea('bibtex')
        inputs = []
        auxnames = []
        outbase = None
        for line in log.splitlines():
            m = re.match('(?:The top-level auxiliary file:'
                         '|A level-[0-9]+ auxiliary file:) (.*)', line)
            if m:
                auxnames.append(os.path.join(cwd, m.group(1)))
                continue
            m = re.match('(?:(The style file:)|(Database file #[0-9]+:)) (.*)',
                         line)
            if m:
                filename = m.group(3)
                if m.group(1):
                    filename = kpathsea.find_file(filename, 'bst', cwd, env)
                elif m.group(2):
                    filename = kpathsea.find_file(filename, 'bib', cwd, env)

                # If this path is relative to the source directory,
                # clean it up for error reporting and portability of
                # the dependency DB
                if filename.startswith('/'):
                    relname = os.path.relpath(filename)
                    if '../' not in relname:
                        filename = relname

                inputs.append(filename)

            # biber output
            m = re.search("Found BibTeX data source '(.*?)'",
                         line)
            if m:
                filename = m.group(1)
                inputs.append(filename)

            m = re.search("Logfile is '(.*?)'", line)
            if m:
                outbase = m.group(1)[:-4]

        if outbase is None:
            outbase = auxnames[0][:-4]

        return inputs, auxnames, outbase

    def report(self):
        extra = self._get_result_extra()
        if extra is None:
            return 0

        # Parse and pretty-print the log
        log = open(extra['outbase'] + '.blg', 'rt').read()
        inputs = extra['inputs']
        for msg in BibTeXFilter(log, inputs).get_messages():
            msg.emit()

        # BibTeX exits with 1 if there are warnings, 2 if there are
        # errors, and 3 if there are fatal errors (sysdep.h).
        # Translate to a normal UNIX exit status.
        if extra['status'] >= 2:
            return 1
        return 0

class BibTeXFilter:
    def __init__(self, data, inputs):
        self.__inputs = inputs
        self.__key_locs = None

        self.__messages = []

        prev_line = ''
        for line in data.splitlines():
            msg = self.__process_line(prev_line, line)
            if msg is not None:
                self.__messages.append(Message(*msg))
            prev_line = line

    def get_messages(self):
        """Return a list of warning and error Messages."""
        # BibTeX reports most errors in no particular order.  Sort by
        # file and line.
        return sorted(self.__messages,
                      key=lambda msg: (msg.filename or '', msg.lineno or 0))

    def __process_line(self, prev_line, line):
        m = None
        def match(regexp):
            nonlocal m
            m = re.match(regexp, line)
            return m

        # BibTeX has many error paths, but luckily the set is closed,
        # so we can find all of them.  This first case is the
        # workhorse format.
        #
        # AUX errors: aux_err/aux_err_return/aux_err_print
        #
        # BST errors: bst_ln_num_print/bst_err/
        # bst_err_print_and_look_for_blank_line_return/
        # bst_warn_print/bst_warn/
        # skip_token/skip_token_print/
        # bst_ext_warn/bst_ext_warn_print/
        # bst_ex_warn/bst_ex_warn_print/
        # bst_mild_ex_warn/bst_mild_ex_warn_print/
        # bst_string_size_exceeded
        #
        # BIB errors: bib_ln_num_print/
        # bib_err_print/bib_err/
        # bib_warn_print/bib_warn/
        # bib_one_of_two_expected_err/macro_name_warning/
        if match('(.*?)---?line ([0-9]+) of file (.*)'):
            # Sometimes the real error is printed on the previous line
            if m.group(1) == 'while executing':
                # bst_ex_warn.  The real message is on the previous line
                text = prev_line
            else:
                text = m.group(1) or prev_line
            typ, msg = self.__canonicalize(text)
            return (typ, m.group(3), int(m.group(2)), msg)

        # overflow/print_overflow
        if match('Sorry---you\'ve exceeded BibTeX\'s (.*)'):
            return ('error', None, None, 'capacity exceeded: ' + m.group(1))
        # confusion/print_confusion
        if match('(.*)---this can\'t happen$'):
            return ('error', None, None, 'internal error: ' + m.group(1))
        # aux_end_err
        if match('I found (no .*)---while reading file (.*)'):
            return ('error', m.group(2), None, m.group(1))
        # bad_cross_reference_print/
        # nonexistent_cross_reference_error/
        # @<Complain about a nested cross reference@>
        #
        # This is split across two lines.  Match the second.
        if match('^refers to entry "'):
            typ, msg = self.__canonicalize(prev_line + ' ' + line)
            msg = re.sub('^a (bad cross reference)', '\\1', msg)
            # Try to give this key a location
            filename = lineno = None
            m2 = re.search(r'--entry "[^"]"', prev_line)
            if m2:
                filename, lineno = self.__find_key(m2.group(1))
            return (typ, filename, lineno, msg)
        # print_missing_entry
        if match('Warning--I didn\'t find a database entry for (".*")'):
            return ('warning', None, None,
                    'no database entry for ' + m.group(1))
        # x_warning
        if match('Warning--(.*)'):
            # Most formats give warnings about "something in <key>".
            # Try to match it up.
            filename = lineno = None
            for m2 in reversed(list(re.finditer(r' in ([^, \t\n]+)\b', line))):
                if m2:
                    filename, lineno = self.__find_key(m2.group(1))
                    if filename:
                        break
            return ('warning', filename, lineno, m.group(1))
        # @<Clean up and leave@>
        if match('Aborted at line ([0-9]+) of file (.*)'):
            return ('info', m.group(2), int(m.group(1)), 'aborted')

        # biber type errors
        if match('^.*> WARN - (.*)$'):
            print ('warning', None, None, m.group(1))
            m2 = re.match("(.*) in file '(.*?)', skipping ...", m.group(1))
            if m2:
                return ('warning', m2.group(2), "0", m2.group(1))
            return ('warning', None, None, m.group(1))

        if match('^.*> ERROR - (.*)$'):
            m2 = re.match("BibTeX subsystem: (.*?), line (\d+), (.*)$", m.group(1))
            if m2:
                return ('error', m2.group(1), m2.group(2), m2.group(3))
            return ('error', None, None, m.group(1))


    def __canonicalize(self, msg):
        if msg.startswith('Warning'):
            msg = re.sub('^Warning-*', '', msg)
            typ = 'warning'
        else:
            typ = 'error'
        msg = re.sub('^I(\'m| was)? ', '', msg)
        msg = msg[:1].lower() + msg[1:]
        return typ, msg

    def __find_key(self, key):
        if self.__key_locs is None:
            p = BibTeXKeyParser()
            self.__key_locs = {}
            for filename in self.__inputs:
                data = open(filename, 'rt', errors='surrogateescape').read()
                for pkey, lineno in p.parse(data):
                    self.__key_locs.setdefault(pkey, (filename, lineno))
        return self.__key_locs.get(key, (None, None))

class BibTeXKeyParser:
    """Just enough of a BibTeX parser to find keys."""

    def parse(self, data):
        IDENT_RE = '(?![0-9])([^\x00-\x20\x80-\xff \t"#%\'(),={}]+)'
        self.__pos, self.__data = 0, data
        # Find the next entry
        while self.__consume('[^@]*@[ \t\n]*'):
            # What type of entry?
            if not self.__consume(IDENT_RE + '[ \t\n]*'):
                continue
            typ = self.__m.group(1)
            if typ == 'comment':
                continue
            start = self.__pos
            if not self.__consume('([{(])[ \t\n]*'):
                continue
            closing, key_re = {'{' : ('}', '([^, \t\n}]*)'),
                               '(' : (')', '([^, \t\n]*)')}[self.__m.group(1)]
            if typ not in ('preamble', 'string'):
                # Regular entry; get key
                if self.__consume(key_re):
                    yield self.__m.group(1), self.__lineno()
            # Consume body of entry
            self.__pos = start
            self.__balanced(closing)

    def __consume(self, regexp):
        self.__m = re.compile(regexp).match(self.__data, self.__pos)
        if self.__m:
            self.__pos = self.__m.end()
        return self.__m

    def __lineno(self):
        return self.__data.count('\n', 0, self.__pos) + 1

    def __balanced(self, closing):
        self.__pos += 1
        level = 0
        skip = re.compile('[{}' + closing + ']')
        while True:
            m = skip.search(self.__data, self.__pos)
            if not m:
                break
            self.__pos = m.end()
            ch = m.group(0)
            if level == 0 and ch == closing:
                break
            elif ch == '{':
                level += 1
            elif ch == '}':
                level -= 1

class Kpathsea:
    def __init__(self, program_name):
        self.__progname = program_name

    def find_file(self, name, format, cwd=None, env=None):
        """Return the resolved path of 'name' or None."""

        args = ['kpsewhich', '-progname', self.__progname, '-format', format,
                name]
        try:
            verbose_cmd(args, cwd, env)
            path = subprocess.check_output(
                args, cwd=cwd, env=env, universal_newlines=True).strip()
        except subprocess.CalledProcessError as e:
            if e.returncode != 1:
                raise
            return None
        if cwd is None:
            return path
        return os.path.join(cwd, path)

if __name__ == "__main__":
    main()
