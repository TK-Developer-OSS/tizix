import glob, os

for root, dirs, files in os.walk(os.path.expanduser('~/z80pack')):
    for f in files:
        if f.endswith('.c') or f.endswith('.h'):
            path = os.path.join(root, f)
            try:
                txt = open(path, 'r', encoding='latin-1').read()
                if 'Op-code trap' in txt:
                    print(f"Found in: {path}")
            except:
                pass
