import subprocess
import time

p = subprocess.Popen(["sh", "python/run_all.sh"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)

buf = ""
start = time.time()
sent = False

while time.time() - start < 25:
    line = p.stdout.readline()
    if not line:
        break
    print(line, end="")
    buf += line
    if "#" in buf and not sent:
        time.sleep(0.5)
        print("\n>>> SENDING date <<<")
        p.stdin.write("date\n")
        p.stdin.flush()
        sent = True

p.kill()
