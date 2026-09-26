import os, sys, pty, time, select, subprocess
CWD=os.path.expanduser('~/z80pack/tizix/arch/z80pack')
SEQ=["mkdir A","mkdir A/B","ls","ls A","cat HELLO.BIN","rm A/B","rm A","ls","df","ps"]
ROUNDS=int(sys.argv[1]) if len(sys.argv)>1 else 40
for rnd in range(ROUNDS):
    subprocess.run(["sync"],check=False); time.sleep(0.2)
    m,s=pty.openpty()
    p=subprocess.Popen(['./cpmsim','-d','disks'],cwd=CWD,stdin=s,stdout=s,stderr=s,close_fds=True)
    os.close(s); log=[]
    def rd(t=3.0,stop=None):
        b=b""; t0=time.time()
        while time.time()-t0<t:
            r,_,_=select.select([m],[],[],0.1)
            if m in r:
                try:c=os.read(m,4096)
                except OSError:break
                if not c:break
                b+=c
                if stop and stop.encode() in b:break
        x=b.decode('latin-1','replace'); log.append(x); return x
    rd(6.0,stop="# ")
    crash=False
    for cmd in SEQ*3:
        os.write(m,(cmd+"\n").encode())
        o=rd(3.0,stop="\r\n# ")
        if "trap" in o or "HALT Op-Code" in o:
            crash=True; break
    try:p.terminate()
    except Exception:pass
    if crash:
        print(f"ROUND {rnd}: CRASH"); print("".join(log)[-1500:]); sys.exit(2)
    print(f"round {rnd}: ok")
print(f"\nBASE: no crash in {ROUNDS} rounds")
