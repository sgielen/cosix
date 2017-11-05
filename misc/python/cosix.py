import os
import sys
import code
import time

original_print = print
def my_print(*args, **kwargs):
  fh = kwargs.pop('file', sys.stdout)
  fl = kwargs.pop('flush', True)
  original_print(*args, file=fh, flush=fl, **kwargs)
print = my_print

class FDWrapper:
  def __init__(self, fd):
    self.fd = fd
  def fileno(self):
    return self.fd

class TerminalClosedError(Exception):
  pass

class SockConsole(code.InteractiveConsole):
  def __init__(self, terminal, locals):
    code.InteractiveConsole.__init__(self, locals, "<SockConsole>")
    self.terminal = terminal

  def raw_input(self, prompt):
    sys.stdout.flush()
    self.terminal.write(bytearray(prompt, "UTF-8"))
    res = self.terminal.readline()
    if res == '':
      raise TerminalClosedError()
    res = res[:-1].decode("UTF-8")
    parsed = str()
    for c in res:
      if c == '\b':
        parsed = parsed[:-1]
      else:
        parsed += c
    return parsed

  def runsource(self, source, filename, symbol="single"):
    # Python has some pretty annoying quirks here. While eval(expr) returns
    # the return value of the expression, compile(eval(expr)) prints the
    # return value. It also doesn't use the Python global print() function,
    # but an internal one, so the output doesn't go to the socket. Also, this
    # works only for expressions; for statements (like 'foo=5') it raises a
    # syntax error exception.
    # So, we first figure out what type of input we get. If it parses as an
    # expression, we run it as such. Otherwise, we assume it is a statement
    # and try to compile and run it as such.

    code = None

    try:
      code = self.compile(source, filename, "eval")
    except:
      pass

    if code is not None:
      # It compiles in eval mode so it's an expression; run it through eval()
      # again so we can print its return value
      try:
        self.print(eval(source, self.locals))
      except SystemExit:
        raise
      except:
        self.showtraceback()
      return False

    # Either malformed or it's a statement, try compiling as a statement
    try:
      code = self.compile(source, filename, symbol)
    except (OverflowError, SyntaxError, ValueError):
      # Input is malformed
      self.showsyntaxerror(filename)
      return False

    if code is None:
      # More input is required
      return True

    # Compiles as a statement, run it as such
    try:
      eval(code, self.locals)
    except SystemExit:
      raise
    except:
      self.showtraceback()

  def print(self, obj):
    if obj is None:
      pass
    elif type(obj) == str:
      print("'%s'" % obj)
    else:
      print(str(obj))

def rm_rf(name, dir_fd):
  try:
    os.unlink(name, dir_fd=dir_fd)
    return
  except IsADirectoryError:
    pass
  except FileNotFoundError:
    return
  fd = -1
  try:
    fd = os.open(name, os.O_RDWR, dir_fd=dir_fd)
    for file in os.listdir(fd):
      rm_rf(file, fd)
    os.rmdir(name, dir_fd=dir_fd)
  except Exception as e:
    print("Failed to recursively remove " + name + ": " + str(e))
  finally:
    if fd >= 0: os.close(fd)

def run_unittests():
  rm_rf('unittests', sys.argdata['tmpdir'])
  os.mkdir('unittests', dir_fd=sys.argdata['tmpdir'])
  dirfd = os.open("unittests", os.O_RDWR, dir_fd=sys.argdata['tmpdir'])

  binfd = os.open("unittests", os.O_RDONLY, dir_fd=sys.argdata['bootfs'])
  procfd = os.program_spawn(binfd,
    {'logfile': sys.argdata['terminal'],
     'tmpdir': FDWrapper(dirfd),
     'nthreads': 1,
    })
  res = os.pdwait(procfd, 0)
  os.close(procfd)
  os.close(binfd)
  return res

def run_unittests_count(count):
  num_success = 0
  num_failures = 0
  while count == 0 or (num_success + num_failures) < count:
    res = run_unittests()
    if res.si_status == 0:
      success="succeeded"
      num_success += 1
    else:
      success="FAILED"
      num_failures += 1
    print("== Unittest iteration %d %s. Total %d successes, %d failures." %
      (num_success + num_failures, success, num_success, num_failures))
    time.sleep(5)

class allocation_tracking():
  def __enter__(self):
    self.send_alloctracker_command(b'1')
    return self

  def __exit__(self, *args):
    self.send_alloctracker_command(b'0')

  @staticmethod
  def send_alloctracker_command(cmd):
    fd = os.open("kernel/alloctracker", os.O_WRONLY, dir_fd=sys.argdata['procfs'])
    os.write(fd, cmd)
    os.close(fd)

  @staticmethod
  def report():
    allocation_tracking.send_alloctracker_command(b'R')

def run_leak_analysis():
  run_unittests()
  with allocation_tracking() as a:
   run_unittests()
   run_unittests()
  run_unittests()
  allocation_tracking.report()

def run_binary(binary):
  binfd = os.open(binary, os.O_RDONLY, dir_fd=sys.argdata['bootfs'])
  procfd = os.program_spawn(binfd,
    {'stdout': sys.argdata['terminal'],
     'tmpdir': FDWrapper(sys.argdata['tmpdir']),
     'networkd': sys.argdata['networkd'],
    })
  res = os.pdwait(procfd, 0)
  os.close(procfd)
  os.close(binfd)
  return res

def run_tests():
  tests = ("pipe_test", "concur_test", "time_test", "tmptest",
    "mmap_test", "unixsock_test", "udptest", "tcptest")
  for test in tests:
    run_binary(test)
  run_unittests()
