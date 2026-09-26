cd $HOME/z80pack/tizix
make clean
make
# run_headless_test.py は起動前に disks/driveb.dsk のフラッシュ完了をポーリングする
# (make 直後の cpmsim 起動で FAT mount 失敗 / 偽 reboot になる競合を回避)。
python3 python/run_headless_test.py
