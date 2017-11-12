import threading
import subprocess

class Cmd(threading.Thread):
    def __init__(self, cmd, effect='stdout'):
        # print('CMD: %s' % cmd)
        self.cmd = cmd
        self.effect = effect
        self.stdout = None
        self.stderr = None
        self.p = None
        super().__init__()

    def run(self):
        self.p = subprocess.Popen(self.cmd,
                                  shell=True,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)

        self.stdout, self.stderr = self.p.communicate()

    def join(self):
        super().join()
        return self

    def end(self):
        self.p.terminate()

    @property
    def pid(self):
        return self.p.pid

    @property
    def returncode(self):
        return self.p.returncode

def run_cmd(cmd, effect='stdout'):
    '''
    Runs a shell command, waits for it to complete, and returns stdout.
    '''
    cmd = Cmd(cmd, effect)
    cmd.start()
    return cmd
