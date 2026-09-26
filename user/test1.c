/* user/test1.c - string.h / stdlib.h / stdio.h 検証テスト */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

/* マルチブロック(>4KB プロセス)検証用のパディング。_CODE(rodata)へ置かれ
   test1.bin が 1 ブロック(4096B)を超える → kexec が連続 2 ブロックへロードする。
   ランタイム添字で全域を走査し、2 ブロック目にまたがる領域まで正しく
   アドレッシングできる(IY+offset)ことも確認する。 */
const char pad_blob[1600] = "tizix multiblock padding blob - forces the test1 image past a single 4KB process block so kexec must allocate two contiguous blocks";

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    {
        unsigned padsum = 0;
        unsigned i;
        for (i = 0; i < 1600; i++)
            padsum += (unsigned char)pad_blob[i];
        printf("pad_blob sum=%u (image spans 2 blocks)\n", padsum);
    }
    puts("=== Test string.h & stdlib.h & stdio.h ===");

    /* 1. string.h テスト */
    char buf1[32];
    char buf2[32];

    strcpy(buf1, "Hello ");
    strcat(buf1, "World!");
    printf("strcat: %s (len=%u)\n", buf1, strlen(buf1));

    strncpy(buf2, "TizixOS", 5);
    buf2[5] = '\0';
    printf("strncpy: %s\n", buf2);

    int cmp1 = strcmp("abc", "abc");
    int cmp2 = strcmp("abc", "abd");
    printf("strcmp: same=%d, diff=%d\n", cmp1, cmp2);

    char *chr = strchr(buf1, 'W');
    if (chr) printf("strchr: found '%s'\n", chr);

    char *sub = strstr(buf1, "World");
    if (sub) printf("strstr: found '%s'\n", sub);

    memset(buf2, 'A', 4);
    buf2[4] = '\0';
    printf("memset: %s\n", buf2);

    memcpy(buf2, buf1, 5);
    buf2[5] = '\0';
    printf("memcpy: %s\n", buf2);

    /* 2. stdlib.h テスト */
    int val1 = atoi("  -1234");
    int val2 = atoi("5678");
    printf("atoi: %d, %d\n", val1, val2);

    printf("abs: abs(-42)=%d\n", abs(-42));

    char numstr[16];
    itoa(255, numstr, 16);
    printf("itoa 255 in hex: %s\n", numstr);

    srand(1234);
    printf("rand: %d, %d, %d\n", rand(), rand(), rand());

    /* 3. stdio.h ファイル操作テスト */
    void *f1 = fopen("FTEST1.TXT", "w");
    void *f2 = fopen("FTEST2.TXT", "w");
    if (f1 && f2) {
        fputs("File1 Line1\n", f1);
        fputs("File2 Data\n", f2);
        fputc('X', f1);
        fputc('\n', f1);
        fclose(f1);
        fclose(f2);
        printf("dual fopen & fwrite ok\n");
    }

    f1 = fopen("FTEST1.TXT", "r");
    if (f1) {
        char linebuf[32];
        if (fgets(linebuf, sizeof(linebuf), f1)) {
            printf("fgets: %s", linebuf);
        }
        fseek(f1, 0, SEEK_SET);
        printf("ftell after seek: %ld\n", ftell(f1));
        int c = fgetc(f1);
        printf("fgetc: %c\n", (char)c);
        fclose(f1);
    }

    /* 4. lstr.s / lstd.s のうち上でカバーしない関数の確認 */
    char *rr = strrchr("a.b.c", '.');
    printf("strrchr: %s\n", rr ? rr : "(null)");        /* expect ".c" */
    printf("strncmp: %d %d\n",
           strncmp("abcXX", "abcYY", 3),                /* expect 0 */
           strncmp("abd", "abc", 3));                   /* expect >0 */
    printf("memcmp: %d\n", memcmp("abc", "abd", 3));    /* expect <0 */
    printf("labs: %d\n", (int)labs(-12345L));           /* expect 12345 */
    printf("atol: %d\n", (int)atol("  -321"));          /* expect -321 */

    puts("=== Test Complete ===");
    return 0;
}
