# tzsh の回帰用(python/test_tzsh.py が `tzsh /usr/share/tzsh/test1.sh foo` で回す)
i=0
while [ $i -lt 5 ]; do
    i=$((i + 1))
done
echo "i=$i"

for x in a b c; do echo -n "$x"; done
echo
case $1 in
    foo|bar) echo "case:fb" ;;
    *)       echo "case:other" ;;
esac

if [ $# -eq 2 ]; then
    echo "argc2"
elif [ $# -eq 1 ]; then
    echo "argc1"
else
    echo "argcN"
fi

true && echo "and-ok"
false || echo "or-ok"
! false && echo "not-ok"

n=0
while true; do
    n=$((n + 1))
    if [ $n -ge 3 ]; then break; fi
done
echo "n=$n"
for k in 1 2 3 4; do
    if [ $k = 2 ]; then continue; fi
    echo -n $k
done
echo

# 外部コマンドと終了コード(mbtest は引数なしで使い方を出して 1 を返す)
hello > /tzsh.out
echo "hello-rc=$?"
mbtest > /tzsh.out
echo "mbtest-rc=$?"

echo first > /tzsh.out
echo second >> /tzsh.out
cat /tzsh.out
read line < /tzsh.out
echo "read:$line"
echo piped | cat
rm /tzsh.out

name="tiz ix"
echo "${name}!"
echo '$name' "$((3 * (2 + 4) % 5))"
exit 7
