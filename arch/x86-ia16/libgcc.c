/* arch/x86-ia16/libgcc.c  --  ia16-gcc に libgcc.a が無いので 32bit 除算を自前供給
 *   (Z80 側の libivt/ivthelpers.c と同じ役回り)。
 *   ff.c が __udivsi3 / __umodsi3 を要求する。8086 の 16bit 命令だけで
 *   ビット単位ロング除算を組む(32bit シフト演算子を使わない = 再帰しない)。
 *   x86 = リトルエンディアン: unsigned long の下位ワードが先。
 */
typedef unsigned long  U32;
typedef unsigned int   U16;

union u32u {
	U32 v;
	struct { U16 lo, hi; } h;
};

static void udivmod(U32 a, U32 b, U32 *q, U32 *r)
{
	union u32u N, D, Q, R;
	U16 carry, bl;
	int i;

	N.v = a; D.v = b;
	Q.v = 0; R.v = 0;

	if (D.v == 0) {                 /* 0 除算: 実装定義。全 1 を返す。 */
		*q = 0xFFFFFFFFUL;
		*r = 0;
		return;
	}

	for (i = 0; i < 32; i++) {
		/* R <<= 1 */
		carry  = (U16)((R.h.lo & 0x8000) ? 1 : 0);
		R.h.lo = (U16)(R.h.lo << 1);
		R.h.hi = (U16)((R.h.hi << 1) | carry);
		/* R bit0 <- N の最上位ビット */
		if (N.h.hi & 0x8000)
			R.h.lo |= 1;
		/* N <<= 1 */
		carry  = (U16)((N.h.lo & 0x8000) ? 1 : 0);
		N.h.lo = (U16)(N.h.lo << 1);
		N.h.hi = (U16)((N.h.hi << 1) | carry);
		/* Q <<= 1 */
		carry  = (U16)((Q.h.lo & 0x8000) ? 1 : 0);
		Q.h.lo = (U16)(Q.h.lo << 1);
		Q.h.hi = (U16)((Q.h.hi << 1) | carry);
		/* if (R >= D) { R -= D; Q |= 1; } */
		if (R.h.hi > D.h.hi || (R.h.hi == D.h.hi && R.h.lo >= D.h.lo)) {
			bl = R.h.lo;
			R.h.lo = (U16)(R.h.lo - D.h.lo);
			R.h.hi = (U16)(R.h.hi - D.h.hi - (bl < D.h.lo ? 1 : 0));
			Q.h.lo |= 1;
		}
	}
	*q = Q.v;
	*r = R.v;
}

U32 __udivsi3(U32 a, U32 b)
{
	U32 q, r;
	udivmod(a, b, &q, &r);
	return q;
}

U32 __umodsi3(U32 a, U32 b)
{
	U32 q, r;
	udivmod(a, b, &q, &r);
	return r;
}

long __divsi3(long a, long b)
{
	U32 q, r;
	int neg = (a < 0) ^ (b < 0);
	udivmod((U32)(a < 0 ? -a : a), (U32)(b < 0 ? -b : b), &q, &r);
	return neg ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
	U32 q, r;
	udivmod((U32)(a < 0 ? -a : a), (U32)(b < 0 ? -b : b), &q, &r);
	return (a < 0) ? -(long)r : (long)r;
}

/* 32bit シフト(fat12min.c が DWORD の >> 9 などで使う)。ia16-gcc は libgcc.a が無いので自前。
 *   16bit 命令だけで 1 ビットずつ(n は小さい)。 */
U32 __lshrsi3(U32 a, int n)
{
	union u32u v;
	v.v = a;
	while (n-- > 0) {
		v.h.lo = (U16)((v.h.lo >> 1) | ((v.h.hi & 1) ? 0x8000u : 0));
		v.h.hi = (U16)(v.h.hi >> 1);
	}
	return v.v;
}

U32 __ashlsi3(U32 a, int n)
{
	union u32u v;
	v.v = a;
	while (n-- > 0) {
		v.h.hi = (U16)((v.h.hi << 1) | ((v.h.lo & 0x8000u) ? 1 : 0));
		v.h.lo = (U16)(v.h.lo << 1);
	}
	return v.v;
}

U32 __ashrsi3(U32 a, int n)
{
	union u32u v;
	U16 sign;
	v.v = a;
	sign = (U16)(v.h.hi & 0x8000u);
	while (n-- > 0) {
		v.h.lo = (U16)((v.h.lo >> 1) | ((v.h.hi & 1) ? 0x8000u : 0));
		v.h.hi = (U16)((v.h.hi >> 1) | sign);
	}
	return v.v;
}
