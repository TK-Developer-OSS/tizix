import os
import subprocess
try:
    p = subprocess.Popen(['./cpmsim'], cwd=os.path.expanduser('~/z80pack/tizix/arch/z80pack'),
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, _ = p.communicate(input=b'hello\nexit\n', timeout=3)
    print(out.decode('latin1', errors='ignore'))
except Exception as e:
    print("Error:", e)
