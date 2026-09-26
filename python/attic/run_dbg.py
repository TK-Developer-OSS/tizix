# Check where the Op-code trap actually happened in test_vi
import os
import subprocess

out = subprocess.run(["python3", "test_vi.py"], capture_output=True, text=True, cwd=os.path.expanduser("~/z80pack/tizix"))
print("stdout:\n", out.stdout)
print("stderr:\n", out.stderr)

log = open(os.path.expanduser('~/z80pack/tizix/test_vi.log')).read()
print("log:\n", log)
