# tzsh の回帰用: 別の tzsh の終了コードを $? で受ける(python/test_tzsh.py)
# (リダイレクトは入れ子にできない: test1.sh の中で > を使うので、ここでは向けない)
tzsh /usr/share/tzsh/test1.sh x y
echo "rc=$?"
