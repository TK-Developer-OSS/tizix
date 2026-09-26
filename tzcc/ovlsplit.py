#!/usr/bin/env python3
# ovlsplit.py ― tzcc --tizix-user の出力を「core + 関数単位オーバーレイ」に割る (tizix #69)
#
#   なぜ後段でやるか:
#     オーバーレイにしたい関数を **C のソースで別ファイルに分けない**ため。
#     user/vi.c は m68k-mega と共有していて、m68k にはオーバーレイ機構が無い
#     (要らない)。C は普通の 1 本のまま、z80 のビルドだけがここで割る。
#     ハードの都合を共有ソースへ漏らさない([[design-principle-hw-isolation]])。
#
#   何をするか:
#     1. .s を関数(`_name::` 〜 次の関数)単位に切る。
#     2. 設定ファイルで指定した関数群を `.area _OVk` へ移す。
#        群の **先頭の関数が入口**(領域の先頭 = ___ovlbase に来る)。
#        2 番目以降はその群の中からしか呼ばれない下請け。
#     3. core から入口への呼び出し(tizix.c の IY 加算グルー)を
#            ld a, #k / ld hl, #___ovlcall (+IY) / call ___sdcc_call_hl
#        に書き換える。___ovlcall(tzcovl.s)が必要なら読み込んで jp する。
#     4. ローダが使う名前 "/bin/<cmd>00.ovl" を _DATA へ置く。
#
#   単一のモジュールのまま割るのが要点:
#     変数(var_*)・文字列(str_*)・tzc_vb は全部 core の _DATA に残るので、
#     オーバーレイは **core の大域変数も core の関数も、グルー無しでそのまま**
#     触れる(#37 の失敗要因 = 「オーバーレイから core を呼べない」は発生しない)。
#     オーバーレイ内の関数のローカル(var_*)も core の _DATA にあるので、
#     オーバーレイを入れ替えても値は失われない。
#
#   ビルドエラーにするもの(黙って壊れる形):
#     - core が オーバーレイ内のラベルを参照している(書き換え対象外の参照)
#     - オーバーレイが 別のオーバーレイ のラベルを参照している
#       (呼んだ瞬間に自分が上書きされる)
#     - オーバーレイが core の **コード内ローカルラベル**(L...)を参照している
#       (area をまたぐ jr になる)
#     - 設定にある関数が .s に無い
#
#   使い方:
#     python3 ovlsplit.py <in.s> <out.s> --cmd vi --conf ovl/vi.ovl
#   設定ファイル: 1 行 1 オーバーレイ。空白区切りで関数名(先頭が入口)。
#     '#' 以降はコメント。行の順番がオーバーレイ番号(1 から)。
#   標準出力に「オーバーレイ数」を 1 行で出す(ovllink.py が使う)。
import re
import sys

FUNC_DEF = re.compile(r'^(_[A-Za-z_0-9]+)::\s*$')
LABEL_DEF = re.compile(r'^([A-Za-z_.$][A-Za-z_0-9.$]*):{1,2}\s*$')
IDENT = re.compile(r'[A-Za-z_.][A-Za-z_0-9.]*')

# tizix.c が出す「IY を足して呼ぶ」グルー(push af/push de は直前、call は 6 行後)
GLUE = ['push af', 'push de', None, 'push iy', 'pop de', 'add hl, de',
        'pop de', 'pop af', 'call ___sdcc_call_hl']


def die(msg):
    sys.stderr.write('ovlsplit.py: ' + msg + '\n')
    sys.exit(1)


def read_conf(path):
    groups = []
    with open(path) as f:
        for ln in f:
            ln = ln.split('#', 1)[0].split()
            if ln:
                groups.append(['_' + n for n in ln])
    return groups


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        die('usage: ovlsplit.py in.s out.s --cmd NAME --conf FILE')
    src, dst = a[0], a[1]
    cmd = conf = None
    i = 2
    while i < len(a):
        if a[i] == '--cmd':
            cmd = a[i + 1]; i += 2
        elif a[i] == '--conf':
            conf = a[i + 1]; i += 2
        else:
            die('unknown option ' + a[i])
    if not cmd or not conf:
        die('--cmd and --conf are required')
    if len(cmd) > 6:
        die('cmd name must be <= 6 chars (8.3: <cmd>NN.ovl)')

    groups = read_conf(conf)
    if not groups:
        die('no overlay in ' + conf)
    if len(groups) > 99:
        die('too many overlays (max 99)')

    with open(src) as f:
        lines = f.read().split('\n')

    # ---- コード部 / データ部の境界 ----
    dstart = None
    for n, ln in enumerate(lines):
        if ln.strip().startswith('.area _DATA'):
            dstart = n
            break
    if dstart is None:
        die('no .area _DATA')
    code, data = lines[:dstart], lines[dstart:]

    # ---- 関数単位に切る。chunks[0] は最初の関数より前(前置き)----
    chunks = [['', []]]
    for ln in code:
        m = FUNC_DEF.match(ln)
        if m:
            chunks.append([m.group(1), []])
        chunks[-1][1].append(ln)
    byname = {c[0]: c for c in chunks if c[0]}

    owner_of_func = {}          # 関数名 -> オーバーレイ番号
    entry = {}                  # 入口関数名 -> オーバーレイ番号
    for k, g in enumerate(groups, 1):
        for fn in g:
            if fn not in byname:
                die('function %s (overlay %d) not found' % (fn, k))
            if fn in owner_of_func:
                die('function %s listed twice' % fn)
            owner_of_func[fn] = k
        entry[g[0]] = k
    if '_main' in owner_of_func:
        die('_main cannot be in an overlay')

    # ---- 各ラベルの持ち主(0 = core)----
    label_owner = {}
    for name, body in chunks:
        k = owner_of_func.get(name, 0)
        for ln in body:
            m = LABEL_DEF.match(ln.strip())
            if m:
                label_owner[m.group(1)] = k

    # ---- 入口への呼び出しを書き換える ----
    nrew = 0
    for name, body in chunks:
        me = owner_of_func.get(name, 0)
        out = []
        j = 0
        while j < len(body):
            s = [x.strip() for x in body[j:j + 9]]
            if len(s) == 9 and all(GLUE[t] is None or s[t] == GLUE[t] for t in range(9)):
                m = re.match(r'ld hl, #(_[A-Za-z_0-9]+)$', s[2])
                if m and m.group(1) in entry and entry[m.group(1)] != me:
                    k = entry[m.group(1)]
                    ind = body[j][:len(body[j]) - len(body[j].lstrip())]
                    out += [ind + 'ld a, #%d' % k,
                            ind + 'ld hl, #___ovlcall',
                            ind + 'push iy',
                            ind + 'pop de',
                            ind + 'add hl, de',
                            ind + 'call ___sdcc_call_hl']
                    j += 9
                    nrew += 1
                    continue
            out.append(body[j])
            j += 1
        body[:] = out

    # ---- 越境参照の検査 ----
    for name, body in chunks:
        me = owner_of_func.get(name, 0)
        for ln in body:
            t = ln.split(';', 1)[0].strip()
            if not t or t.startswith('.') or LABEL_DEF.match(t):
                continue
            parts = t.split(None, 1)
            if len(parts) < 2:
                continue
            for ref in IDENT.findall(parts[1]):
                if ref not in label_owner:
                    continue            # データ / 外部 / レジスタ名
                k = label_owner[ref]
                if k == me:
                    continue
                if me == 0:
                    die('%s (core) references %s in overlay %d: %s' % (name, ref, k, t))
                if k != 0:
                    die('%s (overlay %d) references %s in overlay %d: %s'
                        % (name, me, ref, k, t))
                if not ref.startswith('_'):
                    die('%s (overlay %d) jumps to core-local label %s: %s'
                        % (name, me, ref, t))

    # ---- 文字列リテラルの移動 ----
    #   str_N を参照しているのが 1 つのオーバーレイだけなら、その文字列ごと
    #   オーバーレイへ移す(core の _DATA を空ける)。tizix.c は _DATA の先頭に
    #   スカラ(var_*)を集めて IX の窓から数値オフセットで触るが、文字列は
    #   その後ろに並び **ラベル経由でしか参照されない**ので、抜いても誰の
    #   オフセットも変わらない。書き込まれない(リテラル)ので置き場所も問わない。
    str_users = {}
    for name, body in chunks:
        me = owner_of_func.get(name, 0)
        for ln in body:
            t = ln.split(';', 1)[0]
            for ref in re.findall(r'\bstr_\d+\b', t):
                str_users.setdefault(ref, set()).add(me)
    moved = {k: [] for k in range(1, len(groups) + 1)}
    kept = []
    j = 0
    nstr = 0
    while j < len(data):
        m = re.match(r'^(str_\d+):\s*$', data[j].strip())
        if m:
            e = j + 1
            while e < len(data) and data[e].strip().startswith('.') \
                    and not data[e].strip().startswith('.area'):
                e += 1
            u = str_users.get(m.group(1), set())
            if len(u) == 1 and 0 not in u:
                moved[next(iter(u))] += data[j:e]
                nstr += e - j
                j = e
                continue
        kept.append(data[j])
        j += 1
    data = kept

    # ---- 出力 ----
    out = []
    for name, body in chunks:
        if owner_of_func.get(name, 0) == 0:
            out += body
    out.append('    .globl ___ovlcall')
    out += data
    out += ['    .area _DATA',
            '___ovlname::',
            '    .ascii "/bin/%s"' % cmd,
            '___ovlnum::',
            '    .ascii "00.ovl"',
            '    .db 0']
    for k, g in enumerate(groups, 1):
        out.append('    .area _OV%d' % k)
        for fn in g:
            out += byname[fn][1]
        out += moved[k]
    with open(dst, 'w') as f:
        f.write('\n'.join(out) + '\n')

    sys.stderr.write('ovlsplit.py: %d overlays, %d call sites rewritten, '
                     '%d string lines moved -> %s\n'
                     % (len(groups), nrew, nstr, dst))
    print(len(groups))


if __name__ == '__main__':
    main()
