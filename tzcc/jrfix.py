#!/usr/bin/env python3
# jrfix.py ― tzcc --tizix-user の後段。範囲外になった jr だけを間接化する。
#
#   背景:
#     jr / djnz は相対分岐なのでロード先 base に依存しない。よって tizix.c は
#     jr を一切触らず、セグメント内 jp も jr へ落とす(3B -> 2B、グルー 0B)。
#
#     ただし tizix.c はデータアクセス / call の変換で 1 箇所あたり 8〜12B を
#     挿入するので、その挿入を跨ぐ jr は ±127 を超えることがある。sdasz80 は
#     これを「Branching Range Exceeded」で報告して .rel を消す(= 黙って壊れない)。
#
#   このスクリプトの仕事:
#     sdasz80 を回し、範囲外と報告された jr **だけ** を間接ジャンプ列に置換して
#     再アセンブルする。置換で更に数バイト伸びるので、エラーが消えるまで反復する。
#
#   なぜ全部を先回りして間接化しないか:
#     それが以前の実装で、無条件 jp に 14B・条件 jp に 16B を無条件に払っていた。
#     wc.c ではそれだけで 436B(バイナリの 12%)だった。
#
#   出自: tizix の user/iy_jrfix.py(SDCC + iy_reg_claude.py 用、tizix commit
#     7f4c62a で検証済み)を tzcc へ移植したもの。tzcc を独立リポジトリのまま
#     保つため参照ではなく複製している。挿入列 seq_indirect_jp は tizix.c の
#     間接 JP 列と同一でなければならない(HL/DE/AF・全フラグを保存する)。
#
#   使い方:
#     python3 jrfix.py <in.s> <out.rel> [--as sdasz80] [--max-iter N]
#   in.s は in-place で書き換わる(変換痕跡を残すため意図的にそうしている)。
import io
import os
import re
import subprocess
import sys

import peep

JR_COND = re.compile(r'^(\s*)jr\s+(nz|z|nc|c)\s*,\s*(.+?)\s*$', re.I)
JR_UNCOND = re.compile(r'^(\s*)jr\s+([^,]+?)\s*$', re.I)
INV_JR = {'nz': 'z', 'z': 'nz', 'nc': 'c', 'c': 'nc'}
RANGE_ERR = re.compile(r':(\d+): Error: <a> Branching Range Exceeded')


def seq_indirect_jp(ind, target):
    """tizix.c の間接 JP 列と同一。HL/DE/AF・全フラグを保存する。"""
    return [
        f"{ind}push hl",
        f"{ind}push af",
        f"{ind}push de",
        f"{ind}ld hl, #{target}",
        f"{ind}push iy",
        f"{ind}pop de",
        f"{ind}add hl, de",
        f"{ind}pop de",
        f"{ind}pop af",
        f"{ind}ex (sp), hl",
        f"{ind}ret",
    ]


def main():
    argv = sys.argv[1:]
    asm = rel = None
    assembler = "sdasz80"
    max_iter = 20
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--as':
            assembler = argv[i + 1]; i += 2
        elif a == '--max-iter':
            max_iter = int(argv[i + 1]); i += 2
        elif asm is None:
            asm = a; i += 1
        elif rel is None:
            rel = a; i += 1
        else:
            sys.exit(f"jrfix.py: unexpected argument: {a}")
    if not asm or not rel:
        sys.exit("usage: jrfix.py <in.s> <out.rel> [--as sdasz80] [--max-iter N]")

    # 覗き穴最適化を先に 1 回だけ掛ける(peep.py)。
    #   ASTZ の入口がここしか無いので Makefile を 4 箇所直すよりここから呼ぶ。
    #   **jr の範囲修正より先**でなければならない(畳むと距離が縮む)。
    npeep_cmp, npeep_ldde, npeep_cp, npeep_andor = peep.run(asm)

    fixed_total = 0
    seq_id = 900000
    for it in range(1, max_iter + 1):
        r = subprocess.run([assembler, "-g", "-o", rel, asm],
                           capture_output=True, text=True)
        text = (r.stdout or "") + (r.stderr or "")
        bad = sorted({int(m.group(1)) for m in RANGE_ERR.finditer(text)}, reverse=True)

        if not bad:
            if r.returncode != 0 or not os.path.exists(rel):
                # 範囲外以外のアセンブルエラー。そのまま見せて落とす。
                sys.stderr.write(text)
                sys.exit(r.returncode or 1)
            if fixed_total:
                print(f"jrfix.py: {fixed_total} out-of-range jr made indirect "
                      f"(converged in {it - 1} iteration(s)) -> {rel}")
            if npeep_cmp or npeep_ldde or npeep_cp or npeep_andor:
                print(f"peep.py: {npeep_cmp} cmp + {npeep_ldde} ld-de"
                      f" + {npeep_cp} cp + {npeep_andor} &&/|| collapsed -> {asm}")
            return 0

        lines = io.open(asm, encoding="utf-8").read().split('\n')
        for ln in bad:                       # 降順。前を先に直すと行番号がずれる
            if not (1 <= ln <= len(lines)):
                sys.exit(f"jrfix.py: line {ln} out of range (unexpected)")
            cur = lines[ln - 1]
            m = JR_COND.match(cur)
            if m:
                ind, cc, target = m.group(1), m.group(2).lower(), m.group(3).strip()
                seq_id += 1
                skp = f"L_fixjr_{seq_id}"
                # 条件を反転して jr でスキップ。スキップ先は直後なので必ず近距離。
                lines[ln - 1:ln] = ([f"{ind}jr {INV_JR[cc]}, {skp}"]
                                    + seq_indirect_jp(ind, target)
                                    + [f"{skp}:"])
                fixed_total += 1
                continue
            m = JR_UNCOND.match(cur)
            if m:
                ind, target = m.group(1), m.group(2).strip()
                lines[ln - 1:ln] = seq_indirect_jp(ind, target)
                fixed_total += 1
                continue
            sys.exit(f"jrfix.py: {asm}:{ln} is not a jr: {cur.strip()}\n"
                     f"  (an out-of-range djnz cannot be fixed here; split the source loop)")
        io.open(asm, "w", encoding="utf-8", newline='\n').write('\n'.join(lines))

    sys.exit(f"jrfix.py: did not converge in {max_iter} iterations ({asm})")


if __name__ == '__main__':
    sys.exit(main())
