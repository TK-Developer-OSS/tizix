/* user/dig.c - 名前引き(#109)。gcc 系アーキ(TZ_SYSCALL)だけ。
 *   カーネルの lwIP の DNS(/etc/wifi の dns=、省略時 8.8.8.8)で A レコードを引く。
 *   問い合わせを自分で組み立てるのではないので、引けるのは A のアドレス 1 つだけ
 *   (lwIP の DNS は結果を覚えていて、2 回目以降は問い合わせずに返す = Query time 0)。
 *
 *   使い方: dig name [name ...]
 */
#include "stdio.h"
#include "netcli.h"

int main(int argc, char **argv)
{
        unsigned long addr, ms;
        int i, rc = 0;

        if (argc < 1) {
                printf("usage: dig name [name ...]\n");
                return 1;
        }
        for (i = 0; i < argc; i++) {
                printf(";; QUESTION: %s. IN A\n", argv[i]);
                if (net_resolve(argv[i], &addr, &ms) != 0) {
                        printf(";; no answer (NXDOMAIN, timeout, or no network)\n\n");
                        rc = 1;
                        continue;
                }
                printf("%s.\tIN\tA\t", argv[i]);
                net_pr_ip(addr);
                printf("\n;; Query time: %u msec\n;; SERVER: ", (unsigned)ms);
                net_pr_ip(net_info(NI_DNS));
                printf("\n\n");
        }
        return rc;
}
