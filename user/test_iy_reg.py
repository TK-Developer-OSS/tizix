#!/usr/bin/env python3
"""test_iy_reg.py - iy_reg.py の単体テストおよび検証テストスイート

テスト項目:
1. 即値 / 外部シンボルの除外テスト (IY加算が注入されないこと)
2. ローカルラベル / 関数ラベルへの LD hl/bc/de に対する IY加算注入テスト
3. 近傍ジャンプ (jr) の維持テスト (±110バイト以内の jr は jr のまま維持)
4. 長距離ジャンプ / jp の IY相対間接ジャンプへの変換テスト
5. 内部関数 call の IY相対間接コールへの変換テスト
6. 実行計画マップ (map_text) の生成テスト
7. 入力に IY が含まれていた際のエラー検出テスト
8. 実アセンブリファイル (a.asm, date.asm 等) の変換検証
"""

import unittest
import sys
import os
import re

# user ディレクトリを import パスに追加
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iy_reg


class TestIYReg(unittest.TestCase):

    def test_external_symbols_not_injected(self):
        """外部シンボル (_drv_tbl, _kexit 等) や数値即値には IY 加算が注入されないこと"""
        lines = [
            "\tld\thl, #_drv_tbl\n",
            "\tld\tbc, #(_drv_tbl + 4)\n",
            "\tld\tde, #0x1234\n",
            "\tld\thl, #1000\n",
        ]
        out, injected, map_text = iy_reg.process(lines)
        self.assertEqual(injected, 0, "外部シンボルや即値に IY 加算が注入されてはいけない")
        self.assertEqual(len(out), 4)

    def test_local_labels_injected(self):
        """ローカルラベル (00101$, _func 等) に IY 加算が正しく注入されること"""
        lines = [
            "\tld\thl, #00101$\n",
            "\tld\tbc, #00102$\n",
            "\tld\tde, #_my_local_func\n",
        ]
        out, injected, map_text = iy_reg.process(lines)
        self.assertEqual(injected, 3, "3箇所のローカルラベル全てに注入されること")
        
        out_text = "\n".join(out)
        self.assertIn("push\tiy", out_text)
        self.assertIn("add\thl, de", out_text)
        self.assertIn("add\thl, bc", out_text)
        self.assertIn("[INJECT-PASS1]", map_text)

    def test_near_jr_preserved(self):
        """近傍の jr 命令はそのまま jr として維持されること"""
        lines = [
            "00101$:\n",
            "\tnop\n",
            "\tjr\t00101$\n",
            "\tjr\tZ, 00101$\n",
        ]
        out, _, map_text = iy_reg.process(lines)
        out_text = "\n".join(out)
        self.assertIn("jr\tL_global_00101", out_text)
        self.assertIn("jr\tZ, L_global_00101", out_text)
        self.assertIn("[維持:", map_text)

    def test_call_uses_indirect_call(self):
        """内部関数 call は間接コール (___sdcc_call_hl) に変換されること"""
        lines = [
            "\tcall\t_internal_func\n",
        ]
        out, _, map_text = iy_reg.process(lines)
        out_text = "\n".join(out)
        self.assertIn("___sdcc_call_hl", out_text)
        self.assertIn("push\tiy", out_text)
        self.assertIn("call\t___sdcc_call_hl", out_text)

    def test_jp_must_not_use_call(self):
        """長距離ジャンプ (jp / jr) は call を使わずスタックを破壊しないジャンプに変換されること"""
        lines = [
            "\tjp\t00999$\n",
            "\tjr\t00999$\n",
        ]
        out, _, map_text = iy_reg.process(lines)
        out_text = "\n".join(out)
        self.assertNotIn("___sdcc_call_hl", out_text, "ジャンプ命令 (jp/jr) で call ___sdcc_call_hl を使用してはならない（スタック破壊防止）")
        self.assertIn("ret", out_text, "ジャンプは ex (sp), hl -> ret 等のスタックレス/ジャンプ形式であること")
        self.assertIn("間接ジャンプ化", map_text)

    def test_iy_in_input_rejected(self):
        """入力に既に IY レジスタが使われていた場合はエラー終了すること"""
        lines = [
            "\tld\tiy, #0x1000\n",
        ]
        with self.assertRaises(SystemExit):
            iy_reg.process(lines)

    def test_real_asm_if_exists(self):
        """実ファイル (a.asm や date.asm) が存在する場合の変換テスト"""
        cur_dir = os.path.dirname(os.path.abspath(__file__))
        for asm_name in ["a.asm", "date.asm", "hello.asm"]:
            asm_path = os.path.join(cur_dir, asm_name)
            if os.path.exists(asm_path):
                with open(asm_path, "r") as f:
                    lines = f.readlines()
                out, injected, map_text = iy_reg.process(lines)
                self.assertTrue(len(out) >= len(lines))
                self.assertGreater(len(out), 0)
                self.assertGreater(len(map_text), 0)


def run_tests():
    suite = unittest.TestLoader().loadTestsFromTestCase(TestIYReg)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return result.wasSuccessful()


if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
