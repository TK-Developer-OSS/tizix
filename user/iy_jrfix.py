#!/usr/bin/env python3
# iy_jrfix.py ― iy_reg_claude.py の後段。範囲外になった jr だけを間接化する。
#
#   背景:
#     jr / djnz は相対分岐なのでロード先 base に依存しない。よって iy_reg は
#     jr を一切触らない(iy_reg_claude.py ヘッダ [2] の方針)。これにより sdcc の
#     peephole が出す jr(sh.c で 198 個)はグルー 0 バイトで済む。
#
#     ただし iy_reg は jp/call/ld の変換で 1 箇所あたり 11〜16B を挿入するので、
#     その挿入を跨ぐ jr は ±127 を超えることがある。sdasz80 はこれを
#     「Branching Range Exceeded」で報告して .rel を消す(= 黙って壊れない)。
#
#   このスクリプトの仕事:
#     sdasz80 を回し、範囲外と報告された jr **だけ** を間接ジャンプ列に置換して
#     再アセンブルする。置換で更に数バイト伸びるので、エラーが消えるまで反復する。
#     実測(sh.c): 198 個中 7 個が範囲外、2 反復で収束。
#
#   なぜ全部を先回りして間接化しないか:
#     それが以前の実装で、jr 198 個 × 約 15B = 約 3KB を無条件に払っていた。
#     さらに「そもそも jr を出させない」ために Makefile が --no-peep を付ける
#     運用になり、jp が 405 個へ増えて事態を悪化させていた。
#
#   使い方:
#     python3 iy_jrfix.py <in.iy.asm> <out.rel> [--as sdasz80] [--max-iter N]
#   in.iy.asm は in-place で書き換わる(変換痕跡を残すため意図的にそうしている)。
import io
import os
import re
import subprocess
import sys

JR_COND = re.compile(r'^(\s*)jr\s+(nz|z|nc|c)\s*,\s*(.+?)\s*$', re.I)
JR_UNCOND = re.compile(r'^(\s*)jr\s+([^,]+?)\s*$', re.I)
INV_JR = {'nz': 'z', 'z': 'nz', 'nc': 'c', 'c': 'nc'}
RANGE_ERR = re.compile(r':(\d+): Error: <a> Branching Range Exceeded')


def seq_indirect_jp(ind, target):
    """iy_reg_claude.py の seq_indirect_jp と同一。HL/DE/AF・全フラグを保存。"""
    return [
        f"{ind}push\thl",
        f"{ind}push\taf",
        f"{ind}push\tde",
        f"{ind}ld\thl, #{target}",
        f"{ind}push\tiy",
        f"{ind}pop\tde",
        f"{ind}add\thl, de",
        f"{ind}pop\tde",
        f"{ind}pop\taf",
        f"{ind}ex\t(sp), hl",
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
            sys.exit(f"iy_jrfix.py: 余分な引数: {a}")
    if not asm or not rel:
        sys.exit("usage: iy_jrfix.py <in.iy.asm> <out.rel> [--as sdasz80] [--max-iter N]")

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
                print(f"iy_jrfix.py: {fixed_total} 個の範囲外 jr を間接化 "
                      f"({it - 1} 反復で収束) -> {rel}")
            return 0

        lines = io.open(asm, encoding="utf-8").read().split('\n')
        for ln in bad:                       # 降順。前を先に直すと行番号がずれる
            if not (1 <= ln <= len(lines)):
                sys.exit(f"iy_jrfix.py: 行番号 {ln} が範囲外(想定外)")
            cur = lines[ln - 1]
            m = JR_COND.match(cur)
            if m:
                ind, cc, target = m.group(1), m.group(2).lower(), m.group(3).strip()
                seq_id += 1
                skp = f"L_fixjr_{seq_id}"
                # 条件を反転して jr でスキップ。スキップ先は直後なので必ず近距離。
                lines[ln - 1:ln] = ([f"{ind}jr\t{INV_JR[cc]}, {skp}"]
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
            sys.exit(f"iy_jrfix.py: {asm}:{ln} が jr ではありません: {cur.strip()}\n"
                     f"  (djnz が範囲外の場合はここでは直せない。ソース側で分割すること)")
        io.open(asm, "w", encoding="utf-8", newline='\n').write('\n'.join(lines))

    sys.exit(f"iy_jrfix.py: {max_iter} 反復で収束しませんでした({asm})")


if __name__ == '__main__':
    sys.exit(main())
