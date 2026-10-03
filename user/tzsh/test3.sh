# tzsh の回帰用: リダイレクトの入れ子は断られる(カーネルの向け先は 1 本。python/test_tzsh.py)
tzsh -c 'echo inner > /r2' > /r3
cat /r3
rm /r3
