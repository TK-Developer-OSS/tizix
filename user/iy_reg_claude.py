#!/usr/bin/env python3
# iy_reg_claude.py ― sdcc(-mz80) の .asm 出力を tizix 外部コマンド用に
#                    「IY 相対 位置独立コード(PIC)」へ変換する。
#
# 前提と方針(重要):
#   * ユーザーコマンドは sdldz80 -b _CODE=0x0000 でリンクされ、
#     全ラベルは「セグメント先頭からのオフセット(0x0000 基準)」になる。
#   * 実行時、crt0cmd がロード先ブロックの実ベースを IY にロードしておく。
#   * 本スクリプトは *ビルド時(コンパイル時)* に .asm を変形し、
#     絶対番地を参照する箇所を「実行時に IY(base) を足して実アドレスを作る」
#     コードへ置換する。ロード後にバイナリを書き換える処理は一切しない
#     (= ロード時パッチではない)。
#
# 過去の失敗と、その回避策:
#   [1] バイナリ線形逆アセンブラは命令長を1つ誤ると全ズレし、データ中の
#       0xC3 を jp と誤認し、整数即値をアドレスと誤認する(サイレント破壊)。
#       → 入力を .asm テキストにし、シンボルと数値を構文で厳密に区別する。
#   [2] 命令列を伸ばすと jr の変位が狂う。
#       → jr / djnz には一切触れない。相対分岐は base 非依存。挿入で
#         ±127 を超えた場合は sdasz80 がアセンブル時にエラーを出す
#         (=気づける。黙って壊れない)。範囲外になった少数だけを後段の
#         iy_jrfix.py が間接化する。
#       ※ 一時期この方針に反して jr を全部間接化する実装になっており、
#         そのせいで (i) jr 1 個あたり 14-16B のグルー (ii) 誤変換を避ける
#         ための Makefile --no-peep という二次被害が生じていた。--no-peep は
#         「そもそも jr を出させない」ので jp が激増し(sh.c で 405 個)、
#         結局グルーを全部払っていた。2026-09-08 に方針どおりへ戻した。
#         実測: sh.bin 11589B(3ブロック) -> 6507B(2ブロック)。
#   [3] iy_reg.py 版はレジスタ/フラグ保護の juggling を誤り、HL/DE を破壊した。
#       → 全シーケンスを検証済みの形に置換(下記コメントに各行の状態を明記)。
#   [4] 二重パッチ。
#       → このスクリプトは「素の sdcc 出力」だけを入力に取る。iy_reg.py /
#         iy_reg_bin.py を通した後の物を食わせないこと(ビルド工程から撤去する)。
#
# 使い方:
#   python3 iy_reg_claude.py in.asm -o out.asm [--map out.iymap]
#   python3 iy_reg_claude.py in.asm --check-only   # iy 使用の有無を検査するだけ

import re
import sys
import os

# ---- 正規表現 ------------------------------------------------------------
FUNC_HEADER  = re.compile(r'^;\s*Function\s+([A-Za-z_][A-Za-z0-9_]*)')
LABEL_DEF    = re.compile(r'^([A-Za-z0-9_.$]+):')
GLOBAL_DEF   = re.compile(r'^([_A-Za-z][_A-Za-z0-9]*)::')
DOLLAR_REF   = re.compile(r'(\d+)\$')

# ld {hl|de|bc}, #operand
LD_PAIR_IMM  = re.compile(r'^(\s*)ld\s+(hl|de|bc)\s*,\s*#(.+?)\s*$', re.I)
# jp cc, label / jp label
JP_COND      = re.compile(r'^(\s*)jp\s+(nz|z|nc|c|po|pe|p|m)\s*,\s*(.+?)\s*$', re.I)
JP_UNCOND    = re.compile(r'^(\s*)jp\s+([^,]+?)\s*$', re.I)
# call cc, label / call label
CALL_COND    = re.compile(r'^(\s*)call\s+(nz|z|nc|c|po|pe|p|m)\s*,\s*(.+?)\s*$', re.I)
CALL_UNCOND  = re.compile(r'^(\s*)call\s+([^,]+?)\s*$', re.I)

IY_USE       = re.compile(r'\biy\b', re.I)

# jr cc, label / jr label （condは z/nz/c/nc のみ。jr は他条件を取らない）
JR_COND      = re.compile(r'^(\s*)jr\s+(nz|z|nc|c)\s*,\s*(.+?)\s*$', re.I)
JR_UNCOND    = re.compile(r'^(\s*)jr\s+([^,]+?)\s*$', re.I)

# ---- 固定番地の外部シンボル(触ってはいけない) -----------------------------
# これらは Makefile の -g で固定アドレスに束縛される。IY 加算してはならない。
EXTERNAL_SYMBOLS = {
    "_drv_tbl", "_kexit", "_getticks", "_time_get", "_time_set",
    "_kputchar", "_kgetchar",
    "_f_open", "_f_read", "_f_write", "_f_sync", "_f_close", "_f_lseek",
    "___sdcc_call_hl",
    # ---- FatFs 内部ヘルパ (ff.c から公開=kernel.map の -g で固定番地に束縛) ----
    # cold FS 操作(mkdir/unlink/rename)を外部コマンド化する際、それらが呼ぶ
    # これらは ROM 絶対番地。IY 加算すると base+offset で暴走するため除外する。
    # 現行コマンドはどれも参照しないので、この追加は現行に無害(回帰ゼロ)。
    "_move_window", "_sync_fs", "_clst2sect", "_get_fat", "_put_fat",
    "_remove_chain", "_create_chain", "_dir_clear", "_dir_register",
    "_dir_remove", "_follow_path", "_ld_clust", "_st_clust",
    "_get_ldnumber", "_validate", "_mount_volume",
    "_ld_16", "_ld_32", "_st_16", "_st_32",
    "_dir_sdi", "_dir_read", "_dir_next",
    "_get_fattime", "_GET_FATTIME",
    # 注: memcpy/memset/memcmp はコマンド側 libstr(.rel) にリンクされる
    #     base 相対シンボルなので、ここには入れない(IY 変換対象のまま)。
}

# jp (hl), jp (ix), jp (iy), ret, reti などレジスタ間接/復帰は絶対触らない
JP_INDIRECT = re.compile(r'^\s*jp\s+\(\s*(hl|ix|iy)\s*\)\s*$', re.I)


def strip_comment(line):
    i = line.find(';')
    return line if i < 0 else line[:i]


def is_relocatable_symbol(operand):
    """
    operand(# は除いた文字列) が「IY 加算すべきセグメント内ラベル」か判定する。
    テキストなので数値リテラルとシンボルを構文で厳密に切り分けられる。
      - 数値 (#100, #0x1F, #-4, #'A') は False
      - 外部固定シンボル / それを含む式 は False
      - 先頭が英字/_/. または SDCC ローカルラベル(00123$) は True
    """
    op = operand.strip()
    if not op:
        return False

    # 外部固定シンボルを含む式は触らない (_drv_tbl+4 など)
    for ext in EXTERNAL_SYMBOLS:
        if ext in op:
            return False

    # 先頭の (、# を剥がして先頭トークンを見る
    core = op.lstrip('(').strip()

    # 純粋な数値リテラル
    if re.match(r"^[+-]?(0x[0-9a-fA-F]+|[0-9]+)\s*$", core):
        return False
    # 文字定数
    if core.startswith("'") or core.startswith('"'):
        return False

    m = re.match(r'([A-Za-z_.$][A-Za-z0-9_.$]*)', core)
    if not m:
        return False
    sym = m.group(1)
    if sym in EXTERNAL_SYMBOLS:
        return False
    # 2026-09-17 削除: 以前は「本変換器が生成する内部スキップラベル
    # (L_skipjp_*/L_skipcall_*) は再処理時の保険として対象外」としていたが、
    # (1) 再処理そのものは process() 冒頭の IY_USE チェックで既に禁止済みで
    #     この保険は実質不要だった、(2) それ以前に、SDCC 側は使わない命名
    #     だが本変換器自身が JP_COND/CALL_COND のスキップ先として生成する
    #     L_skipjp_N/L_skipcall_N 自体が「このプロセス自身の再配置対象の
    #     コードラベル」であり除外してはいけないものだった。この誤除外の
    #     せいで jp <cond>, L_skipjp_N が素通しされ(+IY されない絶対番地
    #     のまま)、かつ下の検証パスも同じ関数を使うため自分の見落としを
    #     自分で検出できずにいた(#51 のクラッシュの直接原因。
    #     seq_cond_indirect_jp 呼び出し側のコメント参照)。
    # SDCC ローカルラベル 00123$ もしくは通常のシンボル
    return True


# ---- IY 加算シーケンス(検証済み) ----------------------------------------
# 各シーケンスは「対象ペア = 対象ペア + IY」を行い、他の全レジスタと全フラグを保存する。
# コメント右側は各行実行後の状態(検証用)。

def inj_add_iy_hl(ind):
    # HL = HL + IY, 保存: AF, DE
    return [
        f"{ind}push\taf",          # AF退避
        f"{ind}push\tde",          # DE退避
        f"{ind}push\tiy",          #
        f"{ind}pop\tde",           # DE = IY
        f"{ind}add\thl, de",       # HL = HL + IY   (Cフラグ変化 → AFで保護)
        f"{ind}pop\tde",           # DE 復元
        f"{ind}pop\taf",           # AF 復元
    ]

def inj_add_iy_de(ind):
    # DE = DE + IY, 保存: AF, HL, BC
    return [
        f"{ind}push\taf",
        f"{ind}push\thl",
        f"{ind}push\tiy",
        f"{ind}pop\thl",           # HL = IY
        f"{ind}add\thl, de",       # HL = IY + DE
        f"{ind}ex\tde, hl",        # DE = 結果, HL = 旧IY(捨てる)
        f"{ind}pop\thl",           # HL 復元
        f"{ind}pop\taf",
    ]

def inj_add_iy_bc(ind):
    # BC = BC + IY, 保存: AF, HL
    return [
        f"{ind}push\taf",
        f"{ind}push\thl",
        f"{ind}push\tiy",
        f"{ind}pop\thl",           # HL = IY
        f"{ind}add\thl, bc",       # HL = IY + BC
        f"{ind}ld\tb, h",
        f"{ind}ld\tc, l",          # BC = 結果
        f"{ind}pop\thl",           # HL 復元
        f"{ind}pop\taf",
    ]

INJECT = {"hl": inj_add_iy_hl, "de": inj_add_iy_de, "bc": inj_add_iy_bc}


def seq_indirect_jp(ind, target):
    """
    無条件 jp <target> を「target+IY へジャンプ」に置換。
    [HL保存方式] 全レジスタ(HL/DE/AF)・全フラグを保存する。
      jp (hl) 方式は HL を破壊するため、分岐を跨いで HL が生きている
      場合に値が壊れる(クラッシュせず結果だけ狂う)。よって HL を
      復元する ex (sp),hl / ret 方式を使う。
      push hl         ; [slotA] = 旧HL
      push af / push de
      ld  hl, #target ; HL = target(0基準オフセット)
      push iy / pop de; DE = IY(base)
      add hl, de      ; HL = 実アドレス
      pop de / pop af ; DE/AF(フラグ)復元
      ex (sp), hl     ; (slotA)=実アドレス, HL=旧HL(復元)
      ret             ; 実アドレスへジャンプ, SP平衡
    """
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


def seq_cond_indirect_jp(ind, cond, target):
    """
    条件付き絶対ジャンプ jp <cond>, <target> (cond は po/pe/p/m。jr 不可)を
    「条件成立なら target+IY へジャンプ、不成立なら素通り」に置換する。

    2026-09-17 発見のバグ修正: 呼び出し元(JP_COND ハンドラ)は元々
    「target 側だけ」をこの関数相当の間接ジャンプ列で relocate し、
    "skip"(条件不成立側)の飛び先は L_skipjp_N という自前生成ラベルへの
    素の `jp cond, L_skipjp_N` のまま残していた。L_skipjp_N も target と
    同じくこのプロセス自身の再配置対象コード中のラベルなので、素の jp
    (絶対番地、base 非依存にならない)では IY が乗らず、実行時に
    「未再配置の生オフセット」へ飛んでカーネル/他ブロック領域を実行する
    ワイルドジャンプになっていた(#51 の再現の直接原因、task.md 参照)。
    しかも生成ラベル名が "L_skipjp_" 始まりのため、
    is_relocatable_symbol() 側の「本変換器が生成した内部ラベルは対象外」
    フィルタにまさにこのラベル自身が引っかかり、二重に見過ごされていた。

    Z80 の RET は JP と同じ 8 条件(NZ/Z/NC/C/PO/PE/P/M)を持つ数少ない
    条件付き命令なので、`ret cond` を使えば PO/PE/P/M でも「絶対番地を
    まず積んでおき、条件成立時だけ pop→jump、不成立ならそのまま素通り」
    という位置独立な分岐が組める(jr が使えない条件の唯一の代替)。
      push hl / push af / push de   ; 全保存
      ld  hl, #target                ; HL = target(0基準)
      push iy / pop de               ; DE = IY(base)
      add hl, de                     ; HL = 実アドレス(フラグ破壊、後で pop af 復元)
      pop de / pop af                ; DE 復元、AF(判定フラグ)復元
      ex  (sp), hl                   ; (SP top)=実アドレス, HL=旧HL 復元
      ret <cond>                     ; 条件成立: 実アドレスへ pop→ジャンプ、SP 平衡
      inc sp / inc sp                ; 条件不成立(素通り): 積んだままの実アドレスを
                                      ;   捨てて SP を平衡に戻す。

    2026-09-17 のバグ修正その2(実機で発覚): 上のコメントには当初「pop af で
    捨てる、判定に使い終えた後なのでAFは破棄してよい」と書いていたが誤り
    だった。この skip トランポリンは JP_COND ハンドラから「target 側」の
    無条件トランポリン(seq_indirect_jp)の**直前**に置かれ、不成立側は
    そのままフォールスルーして target 側トランポリンへ突入する。target 側
    トランポリンも push af で現在のフラグを保存 → 処理 → pop af で復元する
    ため、ここで pop af によってフラグを「積んであった実アドレス下位ワード」
    という無関係な値で汚すと、そのゴミフラグが target 側トランポリンの
    push/pop af を素通りしてそのまま後続コードまで伝播してしまう。
    今回のケースでは netcli.c の `while (n < len)` がまさにこれで、
    2段目の比較(符号判定の ret p/m)が1段目の後始末で壊れたフラグを見て
    毎回同じ側に倒れ続け、ループが一切終了しなくなっていた(#54 の直接
    原因。send した内容の後ろに無関係なメモリ内容を延々と送り続ける
    症状で発覚)。`inc sp` は 16bit 実効アドレス演算のみでレジスタ・
    フラグを一切変更しない Z80 命令なので、この後始末に使うべきは
    こちらだった(SDCC 自身も同じ目的でこのイディオムを使っている)。
    """
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
        f"{ind}ret\t{cond}",
        f"{ind}inc\tsp",
        f"{ind}inc\tsp",
    ]


def seq_indirect_call(ind, target):
    """
    call <target> を「target+IY を call」に置換 (___sdcc_call_hl 経由)。
    前提: --sdcccall 0 (引数はスタック渡し) のため HL は引数に使われない。
          call を跨いだ HL 生存は SDCC が前提にしない(caller-saved)ため、
          HL を潰して実アドレスを載せてよい。戻り値(HL/DE)はそのまま透過。
      push af         ; フラグ退避
      push de         ; DE退避
      ld  hl, #target
      push iy / pop de; DE = IY
      add hl, de      ; HL = 実アドレス
      pop de          ; DE 復元
      pop af          ; AF 復元
      call ___sdcc_call_hl
    """
    return [
        f"{ind}push\taf",
        f"{ind}push\tde",
        f"{ind}ld\thl, #{target}",
        f"{ind}push\tiy",
        f"{ind}pop\tde",
        f"{ind}add\thl, de",
        f"{ind}pop\tde",
        f"{ind}pop\taf",
        f"{ind}call\t___sdcc_call_hl",
    ]


# 条件反転(jr で表せるもの)
INV_JR = {"z": "nz", "nz": "z", "c": "nc", "nc": "c"}
# jr で表せない条件(パリティ/符号)。この場合は jp <inv>,skip を使う。
INV_JP = {"po": "pe", "pe": "po", "p": "m", "m": "p"}


def process(lines, check_only=False):
    # --- check-only: 入力に iy 使用があれば即エラー(手書きasm混入検出) ---
    if check_only:
        for n, raw in enumerate(lines, 1):
            if IY_USE.search(strip_comment(raw)):
                sys.exit(f"iy_reg_claude.py: error line {n}: 入力に iy 使用あり: {raw.strip()}")
        return [l.rstrip('\n') for l in lines], 0, "check-only: iy 未使用を確認\n"

    # --- Pass0: SDCC ローカルラベル 00123$ を関数スコープ付き一意名に正規化 ---
    #   00123$ は関数ローカル。jp/call の間接化やラベル参照で衝突しないよう
    #   L_<func>_00123 に展開する。ラベル定義・参照の両方を同じ規則で置換。
    cur = "g"
    norm = []
    for raw in lines:
        line = raw.rstrip('\n')
        mh = FUNC_HEADER.match(line)
        if mh:
            cur = mh.group(1)
        mg = GLOBAL_DEF.match(line)
        if mg:
            cur = mg.group(1).lstrip('_')
        i = line.find(';')
        code, com = (line[:i], line[i:]) if i >= 0 else (line, "")
        code = DOLLAR_REF.sub(lambda m: f"L_{cur}_{m.group(1)}", code)
        norm.append(code + com)

    out = []
    plan = []
    n_inject = n_jp = n_call = 0
    skip_id = 0

    # 変換器が生成した行の index。最後の検証パスが「元コード由来の未変換参照」と
    # 「生成シーケンス内の ld hl,#target(直後に +IY される)」を区別するために使う。
    gen_idx = set()

    def emit_seq(seq_lines):
        for s in seq_lines:
            gen_idx.add(len(out))
            out.append(s)

    for line in norm:
        code = strip_comment(line).strip()

        # 入力に iy が生で混ざっていたら中断(二重パッチ/手書き混入の防止)
        if IY_USE.search(strip_comment(line)):
            sys.exit(f"iy_reg_claude.py: error: 入力に iy 使用あり: {line.strip()}")

        # 触ってはいけないもの: jp (hl)/(ix)/(iy) はそのまま
        if JP_INDIRECT.match(line):
            out.append(line)
            continue

        # 0) jr / djnz は「相対分岐 = base 非依存」なので一切触らない。
        #    これがヘッダ [2] に記した本来の方針。以前ここで jr を jp 相当に
        #    間接化していたが、それには 2 つの実害があった:
        #      (a) 1 個 14-16B のグルーが付く。sdcc が出す jr(sh で 198 個)を
        #          全部その値段で買うことになり、実測で ~5KB がこの分だった。
        #      (b) 変換自体が誤変換の温床になり、回避策として Makefile が
        #          --no-peep を付けて「そもそも jr を出させない」運用になった。
        #          結果 jp が 405 個へ増え、(a) の値段を全部払っていた。
        #    挿入で ±127 を超えた jr は sdasz80 が Branching Range Exceeded で
        #    build を止める(黙って壊れない)。その少数だけを iy_jrfix.py が
        #    後段で間接化する。sh の実測では 198 個中 7 個・2 反復で収束。
        if JR_COND.match(line) or JR_UNCOND.match(line):
            out.append(line)
            continue

        # 1) ld {hl|de|bc}, #label  → ロード後に IY 加算
        m = LD_PAIR_IMM.match(line)
        if m:
            ind, reg, oper = m.group(1), m.group(2).lower(), m.group(3).strip()
            if is_relocatable_symbol(oper):
                gen_idx.add(len(out))     # 直後に +IY を注入するので処理済み扱い
                out.append(line)
                out.extend(INJECT[reg](ind))
                n_inject += 1
                plan.append(f"[LD ] {reg} <- #{oper}  (+IY)")
            else:
                out.append(line)  # 数値/外部シンボル: 素通し
            continue

        # 2) jp cc, label  (絶対条件ジャンプ) → 条件を保って間接ジャンプ化
        m = JP_COND.match(line)
        if m:
            ind, cc, target = m.group(1), m.group(2).lower(), m.group(3).strip()
            if is_relocatable_symbol(target):
                skip_id += 1
                skp = f"L_skipjp_{skip_id}"
                if cc in INV_JR:
                    out.append(f"{ind}jr\t{INV_JR[cc]}, {skp}")   # 近距離確定なのでjr(base非依存)
                    emit_seq(seq_indirect_jp(ind, target))
                    out.append(f"{skp}:")
                else:
                    # po/pe/p/m は jr 不可。skp 自身も target と同じ
                    # relocatable なコードラベルなので、素の jp ではなく
                    # ret cc ベースの間接ジャンプで再配置する
                    # (2026-09-17 修正、詳細は seq_cond_indirect_jp のコメント参照)。
                    emit_seq(seq_cond_indirect_jp(ind, INV_JP[cc], skp))
                    emit_seq(seq_indirect_jp(ind, target))
                    out.append(f"{skp}:")
                n_jp += 1
                plan.append(f"[JP ] {cc}, {target}  (間接化, skip={skp})")
            else:
                out.append(line)
            continue

        # 3) jp label  (絶対無条件ジャンプ) → 間接ジャンプ化
        m = JP_UNCOND.match(line)
        if m:
            ind, target = m.group(1), m.group(2).strip()
            # jp (hl) 等は上で処理済み。ここはラベル形のみ。
            if is_relocatable_symbol(target):
                emit_seq(seq_indirect_jp(ind, target))
                n_jp += 1
                plan.append(f"[JP ] {target}  (間接化)")
            else:
                out.append(line)
            continue

        # 4) call cc, label  → 条件を保って間接コール化
        m = CALL_COND.match(line)
        if m:
            ind, cc, target = m.group(1), m.group(2).lower(), m.group(3).strip()
            if is_relocatable_symbol(target):
                skip_id += 1
                skp = f"L_skipcall_{skip_id}"
                if cc in INV_JR:
                    out.append(f"{ind}jr\t{INV_JR[cc]}, {skp}")   # 近距離確定なのでjr
                    emit_seq(seq_indirect_call(ind, target))
                    out.append(f"{skp}:")
                else:
                    # JP_COND と同じバグ・同じ修正(2026-09-17)。skp も
                    # relocatable なコードラベルなので素の jp は不可。
                    emit_seq(seq_cond_indirect_jp(ind, INV_JP[cc], skp))
                    emit_seq(seq_indirect_call(ind, target))
                    out.append(f"{skp}:")
                n_call += 1
                plan.append(f"[CAL] {cc}, {target}  (間接化, skip={skp})")
            else:
                out.append(line)
            continue

        # 5) call label  → 間接コール化 (___sdcc_call_hl 経由)
        m = CALL_UNCOND.match(line)
        if m:
            ind, target = m.group(1), m.group(2).strip()
            if is_relocatable_symbol(target):
                emit_seq(seq_indirect_call(ind, target))
                n_call += 1
                plan.append(f"[CAL] {target}  (間接化)")
            else:
                out.append(line)  # 外部固定関数の call はそのまま
            continue

        # それ以外(jr/djnz 含む)は一切触らない
        out.append(line)

    # ---- 検証パス: 未変換の再配置参照が 1 個でも残っていたら build を止める ----
    #   これが無いと、変換器が拾い損ねた絶対参照は「0 基準のまま実行される」=
    #   ゼロページへの wild jump になり、実行時に初めて分かる(しかもフレークする。
    #   doc/readme「フレーク wild jump」参照)。ここで落とせばビルド時に分かる。
    #   jr / djnz は相対なので対象外。生成シーケンス内の ld hl,#target は直後に
    #   +IY されるので gen_idx で除外する。
    leaks = []
    for k, ol in enumerate(out):
        oc = strip_comment(ol)
        if JP_INDIRECT.match(oc):
            continue
        for rx, kind in ((JP_COND, "jp cc"), (JP_UNCOND, "jp"),
                         (CALL_COND, "call cc"), (CALL_UNCOND, "call"),
                         (LD_PAIR_IMM, "ld")):
            m = rx.match(oc)
            if not m:
                continue
            tgt = m.group(3 if rx in (JP_COND, CALL_COND, LD_PAIR_IMM) else 2).strip()
            if not is_relocatable_symbol(tgt):
                break
            if k in gen_idx:          # 変換器が生成した行(直後に +IY される)
                break
            leaks.append(f"    {k + 1}: {ol.strip()}   [{kind} -> {tgt}]")
            break
    if leaks:
        sys.stderr.write(
            "iy_reg_claude.py: error: 未変換の再配置参照が %d 件残っています。\n"
            "  そのまま実行すると base 加算されずゼロページへ飛びます。\n"
            "  (sdcc の出力パターンが変換器の想定外。該当行を見て規則を足すこと)\n%s\n"
            % (len(leaks), "\n".join(leaks[:20])))
        sys.exit(1)

    header = (
        f"; --- iy_reg_claude.py 変換サマリ ---\n"
        f";   ld +IY 注入 : {n_inject}\n"
        f";   jp 間接化   : {n_jp}\n"
        f";   call 間接化 : {n_call}\n"
        f"; ----------------------------------\n"
    )
    map_text = header + "\n".join(plan) + "\n"
    total = n_inject + n_jp + n_call
    return out, total, map_text


def main():
    args = sys.argv[1:]
    infile = outfile = mapfile = None
    check_only = False
    i = 0
    while i < len(args):
        a = args[i]
        if a == '-o':
            outfile = args[i + 1]; i += 2
        elif a == '--map':
            mapfile = args[i + 1]; i += 2
        elif a in ('--check-only', '-c'):
            check_only = True; i += 1
        else:
            infile = a; i += 1

    if infile is None:
        sys.exit("usage: iy_reg_claude.py in.asm [-o out.asm] [--map out.iymap] [--check-only]")

    with open(infile, encoding='latin1') as f:
        lines = f.readlines()

    out, total, map_text = process(lines, check_only=check_only)
    text = "\n".join(out) + "\n"

    if mapfile is None:
        mapfile = os.path.splitext(infile)[0] + ".iymap"
    try:
        with open(mapfile, "w", encoding='utf-8') as f:
            f.write(map_text)
    except OSError as e:
        sys.stderr.write(f"iy_reg_claude.py: warn: {mapfile} 書込失敗: {e}\n")

    if outfile:
        with open(outfile, "w", encoding='latin1') as f:
            f.write(text)
        sys.stderr.write(f"iy_reg_claude.py: {total} 箇所変換 -> {outfile} (map: {mapfile})\n")
    else:
        sys.stdout.write(text)
        sys.stderr.write(f"iy_reg_claude.py: {total} 箇所変換 (map: {mapfile})\n")


if __name__ == "__main__":
    main()
