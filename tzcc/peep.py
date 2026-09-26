#!/usr/bin/env python3
# peep.py ― tzcc --tizix-user の出力に掛ける覗き穴最適化。jrfix.py から呼ばれる。
#
#   なぜ生成器(tizix.c)側で直さないか:
#     tizix.c は式を「値を HL に作る」規則で一様に吐く。比較演算も例外ではなく、
#     `a == b` は **HL に 0/1 を作ってから** if が「HL が 0 か」を見る。
#     これを直すには「この式の消費者は分岐である」という文脈を生成器へ通す
#     必要があり、式生成のほぼ全域に手が入る。一方この形は出力側では完全に
#     定型なので、後段で畳む方が blast radius が小さい(jrfix.py と同じ立場)。
#
#   畳む形:
#         or a / sbc hl, de      ← フラグを立てる(このスクリプトは触らない)
#         ld hl, #1              ─┐
#         jr z, LcmpN             │ 「フラグ → HL = 0/1」
#         ld hl, #0               │
#     LcmpN:                      │
#         ld a, h                 │
#         or l                   ─┘
#         jr z, Lend             ← 「HL が 0 なら飛ぶ」
#     ↓
#         jr nz, Lend            ← フラグを直に見る
#
#     12B -> 2B。**vi.c で 127 箇所 = 約 1.2KB**。
#
#   もう 1 つ(collapse_ldde): 定数を DE へ置くのに HL を経由する遠回り:
#         push hl / ld hl, #K / pop de / ex de, hl   ->  ld de, #K        (6B -> 3B)
#         push hl / ld hl, #K / pop de               ->  ld de, #K / ex de, hl (5B -> 4B)
#     どちらもレジスタ・フラグまで完全に等価。vi.c で 121 箇所。
#
#   安全性の根拠:
#     - 置換後の jr はフラグを立てた命令の直後に来る(間に挟まるのは
#       `ld hl,#imm` だけで、これはフラグを変えない)。よって条件は同じ。
#     - LcmpN は比較 1 個につき 1 個で、参照も 1 箇所。**実際に参照数を数えて
#       1 でなければ畳まない**(&& / || の短絡が作る Land/Lor は別名なので
#       このパターンには掛からない)。
#     - 畳むと HL と A の値が変わる(元: 0/1、後: 比較の差分)。tzcc は
#       条件式の後で HL/A を読み直さない(必ず IX スロットから load し直す)
#       ので依存が無い ── これは回帰スイート全通過で裏を取っている。
import io
import re
import sys

LD_HL_01 = re.compile(r'^(\s*)ld hl, #([01])$')
JR_TO_CMP = re.compile(r'^\s*jr (z|nz|c|nc), (Lcmp\d+)$')
LD_A_H = re.compile(r'^\s*ld a, h$')
OR_L = re.compile(r'^\s*or l$')
JR_TEST = re.compile(r'^(\s*)jr (z|nz), (.+?)\s*$')
CMP_LABEL = re.compile(r'\bLcmp\d+\b')
INV = {'z': 'nz', 'nz': 'z', 'c': 'nc', 'nc': 'c'}


def collapse_bool(lines):
    """「フラグ→0/1→テスト」を「フラグを直に見る jr」へ畳む。(新 lines, 畳んだ数)"""
    refs = {}
    for ln in lines:
        s = ln.strip()
        if s.endswith(':') and CMP_LABEL.fullmatch(s[:-1] or ' '):
            continue                        # ラベル定義そのものは参照に数えない
        for name in CMP_LABEL.findall(ln):
            refs[name] = refs.get(name, 0) + 1

    out = []
    i = 0
    n = 0
    last = len(lines) - 6
    while i < len(lines):
        m1 = LD_HL_01.match(lines[i]) if i < last else None
        if m1:
            m2 = JR_TO_CMP.match(lines[i + 1])
            m3 = LD_HL_01.match(lines[i + 2])
            m6 = JR_TEST.match(lines[i + 6])
            if (m2 and m3 and m6
                    and m1.group(2) != m3.group(2)          # 1/0 の組でなければ別物
                    and refs.get(m2.group(2)) == 1          # 参照は 1 箇所だけ
                    and lines[i + 3].strip() == m2.group(2) + ':'
                    and LD_A_H.match(lines[i + 4])
                    and OR_L.match(lines[i + 5])):
                cc = m2.group(1)
                # HL が 1 になるのは cc 成立時か? / 最後の jr は「真なら飛ぶ」か?
                same = (m1.group(2) == '1') == (m6.group(2) == 'nz')
                out.append("%sjr %s, %s" % (m1.group(1),
                                            cc if same else INV[cc],
                                            m6.group(3)))
                i += 7
                n += 1
                continue
        out.append(lines[i])
        i += 1
    return out, n



PUSH_HL = re.compile(r'^(\s*)push hl$')
LD_HL_IMM = re.compile(r'^\s*ld hl, (#.+?)\s*$')
POP_DE = re.compile(r'^\s*pop de$')
EX_DE_HL = re.compile(r'^\s*ex de, hl$')


def collapse_ldde(lines):
    """定数を DE へ置くための HL 経由の遠回りを ld de, #K へ畳む。(新 lines, 畳んだ数)

    tzcc は二項演算の右辺を必ず HL に作ってから DE へ移すので、定数でも
        push hl / ld hl, #K / pop de [/ ex de, hl]
    と書く。これは **どちらの形もレジスタ・フラグまで含めて完全に等価**:
        ex 付き(6B) … HL 保存 / DE = K              -> ld de, #K            (3B)
        ex 無し(5B) … HL = K  / DE = 元の HL(入れ替え) -> ld de, #K / ex de, hl (4B)
    ex 付きは `sbc hl, de`(順序が要る)の前、ex 無しは `add hl, de`(可換)の前に出る。
    """
    out = []
    i = 0
    n = 0
    while i < len(lines):
        m1 = PUSH_HL.match(lines[i]) if i + 2 < len(lines) else None
        if m1:
            m2 = LD_HL_IMM.match(lines[i + 1])
            if m2 and POP_DE.match(lines[i + 2]):
                ind = m1.group(1)
                out.append("%sld de, %s" % (ind, m2.group(1)))
                if i + 3 < len(lines) and EX_DE_HL.match(lines[i + 3]):
                    i += 4                  # ex はまるごと消える
                else:
                    out.append("%sex de, hl" % ind)
                    i += 3
                n += 1
                continue
        out.append(lines[i])
        i += 1
    return out, n


LD_A_ANY = re.compile(r'^(\s*)ld a, (.+?)\s*$')
LD_L_A = re.compile(r'^\s*ld l, a$')
LD_H_0 = re.compile(r'^\s*ld h, #0$')
LD_DE_IMM = re.compile(r'^\s*ld de, #(.+?)\s*$')
OR_A = re.compile(r'^\s*or a$')
SBC_HL_DE = re.compile(r'^\s*sbc hl, de$')
JR_CC = re.compile(r'^\s*jr (z|nz|c|nc), .+$')


def collapse_cp(lines):
    """8bit 変数と 0..255 の定数の比較を `cp` 1 個へ。(新 lines, 畳んだ数)

    tzcc は char も一度 HL へゼロ拡張してから 16bit で引き算する:
        ld a, -16(ix) / ld l, a / ld h, #0      ← hl = 0x00AA
        ld de, #0x68 / or a / sbc hl, de        ← 16bit 減算
        jr nz, Lend
    ゼロ拡張した 8bit 同士なら **Z も C も `cp` と完全に一致する**
    (hl < de  <=>  a < n)。よって 9B を `cp #n`(2B)へ畳める = **1 箇所 -7B**。

    安全性:
      - `ld l, a / ld h, #0` が**直前にあること**がゼロ拡張の保証。これが無い形
        (16bit 変数)には掛からない。
      - 定数が 0..255 に収まるときだけ(`cp` は 8bit 即値しか取れない)。
        シンボルや 256 以上は読み飛ばす。
      - 後続は `jr z/nz/c/nc` だけを見る。`cp` が壊すのは F だけで、元の列が
        壊していた HL / DE は**むしろ保存される**(壊れた値に依存はできない)。
      - collapse_bool → collapse_ldde の後に回すこと(この形はその 2 つが
        `jr cc` と `ld de, #K` を作って初めて現れる)。
    """
    out = []
    i = 0
    n = 0
    while i < len(lines):
        m1 = LD_A_ANY.match(lines[i]) if i + 6 < len(lines) else None
        if m1:
            m4 = LD_DE_IMM.match(lines[i + 3])
            if (m4 and LD_L_A.match(lines[i + 1]) and LD_H_0.match(lines[i + 2])
                    and OR_A.match(lines[i + 4])
                    and SBC_HL_DE.match(lines[i + 5])
                    and JR_CC.match(lines[i + 6])):
                try:
                    k = int(m4.group(1), 0)
                except ValueError:
                    k = -1                      # シンボルなどは畳まない
                if 0 <= k <= 255:
                    out.append(lines[i])
                    out.append("%scp #%s" % (m1.group(1), m4.group(1)))
                    out.append(lines[i + 6])
                    i += 7
                    n += 1
                    continue
        out.append(lines[i])
        i += 1
    return out, n


ANDOR_END = re.compile(r'^(L(?:ae|oe)\d+):$')
ANDOR_LBL = re.compile(r'^L(?:and|ae|or|oe|of)\d+$')
JR_LINE = re.compile(r'^(\s*)jr (?:(z|nz|c|nc), )?(\S+)$')
LREF = re.compile(r'\bL\w+')


def _refs(lines):
    refs = {}
    for ln in lines:
        s = ln.split(';', 1)[0].strip()
        if not s or s.endswith(':'):
            continue
        for t in LREF.findall(s):
            refs[t] = refs.get(t, 0) + 1
    return refs


def _next_insn(lines, i):
    """i 以降で最初の「ラベルでも空行でもない」行の添字(無ければ len)"""
    while i < len(lines):
        s = lines[i].strip()
        if s and not s.startswith(';') and not s.endswith(':'):
            return i
        i += 1
    return i


def collapse_andor(lines):
    """`&&` / `||` の 0/1 化を分岐へ畳む。(新 lines, 畳んだ数)

    tzcc は `a && b` / `a || b` を必ず値(HL = 0/1)として作る:
            ...                      ← 左右の判定(偽/真で Land/Lor/Lof へ飛ぶ)
            ld hl, #1 / jr LaeN      ← 真
        LandN:
            ld hl, #0                ← 偽(落ちてくる)
        LaeN:
            ld a, h / or l / jr z, T ← if がその 0/1 をもう一度判定する
    消費者が分岐のとき(LaeN/LoeN の直後が `ld a,h / or l / jr z|nz`)だけ、
        真 → `jr z` なら続きへ / `jr nz` なら T へ
        偽 → `jr z` なら T へ   / `jr nz` なら続きへ
    と直接飛ばす。値として使う形(代入・return)はこの並びにならないので触らない。

    安全性:
      - LaeN/LoeN への参照が **全部 `ld hl, #1` の直後の `jr`** で、落ちてくる
        直前が `ld hl, #0` のときだけ畳む(= そのラベルに来る経路の HL が
        全部 0/1 定数だと言える)。1 つでも外れたら触らない。
      - 畳むと HL と A の値が変わる。tzcc は条件式の後で HL/A を読み直さない
        (collapse_bool と同じ前提。回帰スイートで裏を取る)。
      - 後片付け(分岐の張り替え・直後への jr・参照ゼロのラベルの削除)は
        **このパスが作る Land/Lae/Lor/Loe/Lof 系のラベルにだけ**掛ける。
        到達しない jr の削除だけは一般に成り立つ(無条件 jr / ret の直後で
        ラベルを挟まない jr は誰も実行しない)。
    """
    n = 0
    lines = list(lines)
    while True:
        refs = _refs(lines)
        done = False
        for e, ln in enumerate(lines):
            m = ANDOR_END.match(ln.strip())
            if not m or e + 3 >= len(lines) or e < 1:
                continue
            x = m.group(1)
            mt = JR_TEST.match(lines[e + 3])
            if not (LD_A_H.match(lines[e + 1]) and OR_L.match(lines[e + 2]) and mt):
                continue
            if lines[e - 1].strip() != 'ld hl, #0':
                continue
            jr_idx = [i for i, l2 in enumerate(lines) if l2.strip() == 'jr ' + x]
            if refs.get(x, 0) != len(jr_idx):
                continue
            if any(i < 1 or lines[i - 1].strip() != 'ld hl, #1' for i in jr_idx):
                continue
            ind, cc, tgt = mt.group(1), mt.group(2), mt.group(3)
            jset = set(jr_idx)
            new = []
            for i, l2 in enumerate(lines):
                if i in (e + 1, e + 2, e + 3):
                    continue                        # 0/1 の再判定を消す
                if i + 1 in jset:
                    continue                        # 真側の ld hl, #1
                if i in jset:
                    new.append(ind + ('jr ' + x if cc == 'z' else 'jr ' + tgt))
                    continue
                if i == e - 1:                      # 偽側の ld hl, #0
                    if cc == 'z':
                        new.append(ind + 'jr ' + tgt)
                    continue
                new.append(l2)
            lines = new
            n += 1
            done = True
            break
        if not done:
            break

    # ---- 後片付け ----
    while True:
        changed = False
        # (1) 先頭が無条件 jr の Land 系ラベルへの分岐を、その先へ張り替える
        thread = {}
        for i, ln in enumerate(lines):
            s = ln.strip()
            if s.endswith(':') and ANDOR_LBL.match(s[:-1]):
                j = _next_insn(lines, i + 1)
                if j < len(lines):
                    mj = JR_LINE.match(lines[j])
                    if mj and mj.group(2) is None and mj.group(3) != s[:-1]:
                        thread[s[:-1]] = mj.group(3)
        for i, ln in enumerate(lines):
            mj = JR_LINE.match(ln)
            if mj and mj.group(3) in thread:
                lines[i] = (mj.group(1) + 'jr '
                            + (mj.group(2) + ', ' if mj.group(2) else '')
                            + thread[mj.group(3)])
                changed = True
        # (2) 無条件 jr / ret の直後の(ラベルを挟まない)jr は到達しない
        out = []
        dead = False
        for ln in lines:
            s = ln.split(';', 1)[0].strip()
            if s.endswith(':'):
                dead = False
            elif dead and JR_LINE.match(ln):
                changed = True
                continue
            elif s and not s.startswith('.'):
                mj = JR_LINE.match(ln)
                dead = (mj is not None and mj.group(2) is None) or s == 'ret'
            out.append(ln)
        lines = out
        # (3) 直後の Land 系ラベルへの無条件 jr / 参照ゼロの Land 系ラベル
        refs = _refs(lines)
        out = []
        for i, ln in enumerate(lines):
            s = ln.strip()
            mj = JR_LINE.match(ln)
            if mj and mj.group(2) is None and ANDOR_LBL.match(mj.group(3)):
                k = i + 1
                hit = False
                while k < len(lines) and lines[k].strip().endswith(':'):
                    if lines[k].strip()[:-1] == mj.group(3):
                        hit = True
                    k += 1
                if hit:
                    changed = True
                    continue
            if s.endswith(':') and ANDOR_LBL.match(s[:-1]) and refs.get(s[:-1], 0) == 0:
                changed = True
                continue
            out.append(ln)
        lines = out
        if not changed:
            break
    return lines, n


def run(path):
    """path(.s)を in-place で畳む。(比較, ld de, cp, &&/||) それぞれの件数を返す。"""
    lines = io.open(path, encoding="utf-8").read().split('\n')
    lines, nb = collapse_bool(lines)
    lines, nd = collapse_ldde(lines)
    lines, nc = collapse_cp(lines)
    lines, na = collapse_andor(lines)
    if nb or nd or nc or na:
        io.open(path, "w", encoding="utf-8", newline='\n').write('\n'.join(lines))
    return nb, nd, nc, na


if __name__ == '__main__':
    for p in sys.argv[1:]:
        print("peep.py: %s: cmp=%d ldde=%d cp=%d andor=%d" % ((p,) + run(p)))
