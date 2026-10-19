/* tizix x86-ia16 : COM1 16550 UART 物理層 & BIOS コンソール併用 (M1)
 *   QEMU / 実機対応:
 *   - con_init: 16550 の存在を検出 (scratch レジスタ COM1+7 に 0x5A/0xA5 を書いて読み戻す)。
 *     無ければ serial_ok = 0 にして COM1 への出力/受信確認をスキップ。
 *   - 出力: COM1 と BIOS 画面 (INT 10h AH=0Eh, BH=0, BL=7) の両方へ。
 *   - 入力: COM1 受信と BIOS キーボード (INT 16h AH=01h/00h) の両方から。
 *     特殊キー(上=48h→ESC [ A, 下=50h→B, 右=4Dh→C, 左=4Bh→D)に変換。
 */
#define COM1 0x3F8

static int serial_ok = 0;

/* 特殊キー変換用バッファ */
static unsigned char kbd_buf[8];
static int kbd_buf_len = 0;
static int kbd_buf_idx = 0;

static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %%al, %%dx" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port)
{
	unsigned char r;
	__asm__ volatile ("inb %%dx, %%al" : "=a"(r) : "d"(port));
	return r;
}

static inline void io_delay(void)
{
	__asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

void con_init(void)
{
	/* 16550 存在検出: scratch レジスタ (COM1 + 7) にテスト値を書き込み読み戻す */
	outb(COM1 + 7, 0x5A);
	io_delay();
	if (inb(COM1 + 7) == 0x5A) {
		outb(COM1 + 7, 0xA5);
		io_delay();
		if (inb(COM1 + 7) == 0xA5) {
			serial_ok = 1;
		}
	}

	if (serial_ok) {
		outb(COM1 + 1, 0x00);   /* 割り込み無効                        */
		outb(COM1 + 3, 0x80);   /* DLAB=1                              */
		outb(COM1 + 0, 0x01);   /* 分周比 lo = 1 -> 115200bps          */
		outb(COM1 + 1, 0x00);   /* 分周比 hi = 0                       */
		outb(COM1 + 3, 0x03);   /* 8N1, DLAB=0                         */
		outb(COM1 + 2, 0xC7);   /* FIFO 有効・クリア・14byte 閾値      */
		outb(COM1 + 4, 0x0B);   /* DTR|RTS|OUT2                        */
	}

	/* IRQ1(キーボード)は PIC でマスク: BIOS の INT 09h に取らせず、下の kb_poll が読む */
	outb(0x21, (unsigned char)(inb(0x21) | 0x02));
}

/* 画面(VGA テキスト 80x25, B800:0000)へ直接書く。BIOS の INT 10h は使わない
 * (BIOS が中で sti し、IF=0 のカーネル中に PIT が割り込んで _isr08 が
 * syscall の途中で走りハングするため)。カーソルは CRTC(0x3D4/0x3D5)で更新。 */
#define VCOLS 80
#define VROWS 25
static unsigned char vrow, vcol, vinit;

static void vwrite(unsigned off, unsigned val)
{
	__asm__ volatile (
		"push %%es\n\t"
		"push %%ax\n\t"
		"mov $0xB800, %%ax\n\t"
		"mov %%ax, %%es\n\t"
		"pop %%ax\n\t"
		"mov %%ax, %%es:(%%bx)\n\t"
		"pop %%es"
		: : "a"(val), "b"(off) : "memory", "cc");
}

static void vscroll(void)
{
	unsigned i;
	unsigned si_ = VCOLS * 2, di_ = 0, cx_ = (VROWS - 1) * VCOLS;
	/* 1 行上へ: B800 内の rep movsw(DS/ES を一時的に B800 にする) */
	__asm__ volatile (
		"push %%ds\n\t"
		"push %%es\n\t"
		"mov $0xB800, %%ax\n\t"
		"mov %%ax, %%ds\n\t"
		"mov %%ax, %%es\n\t"
		"cld\n\t"
		"rep movsw\n\t"
		"pop %%es\n\t"
		"pop %%ds"
		: "+S"(si_), "+D"(di_), "+c"(cx_)
		: : "ax", "memory", "cc");
	for (i = (VROWS - 1) * VCOLS; i < VROWS * VCOLS; i++)
		vwrite(i * 2, 0x0720);
}

static void vcursor(void)
{
	unsigned pos = (unsigned)vrow * VCOLS + vcol;
	outb(0x3D4, 0x0F); outb(0x3D5, (unsigned char)(pos & 0xFF));
	outb(0x3D4, 0x0E); outb(0x3D5, (unsigned char)(pos >> 8));
}

/* ANSI(VT100 の一部)。vi が使う ESC[2J / ESC[H / ESC[r;cH / ESC[K / ESC[nm と、
 * 矢印の ESC[A..D、保存/復帰 ESC[s / ESC[u だけを解釈する(それ以外の ESC[..x は捨てる)。 */
static unsigned char esc_st, esc_n, esc_have, vattr = 0x07, sv_row, sv_col;
static unsigned esc_p[4];

static void v_clear_range(unsigned from, unsigned to)   /* [from,to) のセル番号 */
{
	unsigned i;
	for (i = from; i < to; i++)
		vwrite(i * 2, 0x0720);
}

static void v_csi(unsigned char fc)
{
	unsigned n0 = esc_p[0], i, cur = (unsigned)vrow * VCOLS + vcol;
	unsigned np = esc_n + (esc_have || esc_n) ;   /* 渡されたパラメータ数 */

	switch (fc) {
	case 'H': case 'f': {
		unsigned r = esc_p[0] ? esc_p[0] : 1, c = esc_p[1] ? esc_p[1] : 1;
		vrow = (unsigned char)(r > VROWS ? VROWS - 1 : r - 1);
		vcol = (unsigned char)(c > VCOLS ? VCOLS - 1 : c - 1);
		break; }
	case 'J':
		if (n0 == 2) v_clear_range(0, VROWS * VCOLS);
		else if (n0 == 1) v_clear_range(0, cur + 1);
		else v_clear_range(cur, VROWS * VCOLS);
		break;
	case 'K': {
		unsigned ls = (unsigned)vrow * VCOLS, le = ls + VCOLS;
		if (n0 == 2) v_clear_range(ls, le);
		else if (n0 == 1) v_clear_range(ls, cur + 1);
		else v_clear_range(cur, le);
		break; }
	case 'A': { unsigned d = n0 ? n0 : 1; vrow = (unsigned char)(vrow > d ? vrow - d : 0); break; }
	case 'B': { unsigned d = n0 ? n0 : 1; vrow = (unsigned char)(vrow + d >= VROWS ? VROWS - 1 : vrow + d); break; }
	case 'C': { unsigned d = n0 ? n0 : 1; vcol = (unsigned char)(vcol + d >= VCOLS ? VCOLS - 1 : vcol + d); break; }
	case 'D': { unsigned d = n0 ? n0 : 1; vcol = (unsigned char)(vcol > d ? vcol - d : 0); break; }
	case 's': sv_row = vrow; sv_col = vcol; break;
	case 'u': vrow = sv_row; vcol = sv_col; break;
	case 'm':
		if (!np) vattr = 0x07;
		for (i = 0; i < np && i < 4; i++) {
			unsigned v = esc_p[i];
			if (v == 0 || v == 27 || v == 22) vattr = 0x07;
			else if (v == 1) vattr |= 0x08;
			else if (v == 7) vattr = 0x70;
		}
		break;
	default: break;
	}
}

static void vga_putc(unsigned char c)
{
	unsigned i;

	if (!vinit) {                       /* 初回: 画面を消して左上から */
		for (i = 0; i < VROWS * VCOLS; i++)
			vwrite(i * 2, 0x0720);
		vrow = 0; vcol = 0; vinit = 1;
	}
	if (esc_st == 1) {                  /* ESC の次 */
		if (c == '[') { esc_st = 2; esc_n = 0; esc_have = 0; esc_p[0] = esc_p[1] = esc_p[2] = esc_p[3] = 0; return; }
		esc_st = 0;                     /* ESC 単独は捨てて、この文字は通常どおり */
	} else if (esc_st == 2) {           /* CSI のパラメータ/最終文字 */
		if (c >= '0' && c <= '9') { esc_p[esc_n] = esc_p[esc_n] * 10 + (c - '0'); esc_have = 1; return; }
		if (c == ';') { if (esc_n < 3) esc_n++; esc_have = 0; return; }
		if (c == '?') return;           /* ESC[?25l など(カーソル表示切替)は無視 */
		esc_st = 0;
		v_csi(c);
		vcursor();
		return;
	}
	if (c == 27) { esc_st = 1; return; }
	if (c == '\r') {
		vcol = 0;
	} else if (c == '\n') {
		/* 画面では LF を CR+LF として扱う(#184)。通常はカーネルが \n を \r\n にして送るが、
		 * 経路によって素の \n だけが来ると階段状に右へずれて「左端から始まらない」になるため。 */
		vcol = 0;
		vrow++;
	} else if (c == '\b') {
		if (vcol) vcol--;
	} else if (c == '\t') {
		vcol = (unsigned char)((vcol + 8) & ~7);
	} else if (c >= 0x20) {
		vwrite(((unsigned)vrow * VCOLS + vcol) * 2, ((unsigned)vattr << 8) | c);
		vcol++;
	}
	if (vcol >= VCOLS) { vcol = 0; vrow++; }
	if (vrow >= VROWS) { vscroll(); vrow = VROWS - 1; }
	vcursor();
}

void con_putc(char c)
{
	{
		unsigned fl;
		/* vrow/vcol/VRAM 更新は割り込み禁止で行う(カーネル(IF=1)と子の syscall(IF=0)が
		 * 同時に印字すると、途中でタイマに割り込まれて座標が壊れる)。 */
		__asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(fl) : : "memory", "cc");
		vga_putc((unsigned char)c);
		__asm__ volatile ("push %0\n\tpopf" : : "r"(fl) : "memory", "cc");
	}

	/* COM1 出力 (serial_ok のみ) */
	if (serial_ok) {
		while (!(inb(COM1 + 5) & 0x20))   /* LSR THRE 待ち */
			;
		outb(COM1, (unsigned char)c);
	}
}

/* キーボード: BIOS(INT 16h / BDA)には頼らず、8042(ポート 0x60/0x64)を直接ポーリングして
 * スキャンコード(セット 1)を変換する。理由: (1) INT 16h は中で sti するので IF=0 の
 * カーネル中に PIT が割り込み、_isr08 が syscall の途中で走ってハングする。
 * (2) BDA バッファを埋める BIOS の INT 09h は IF=1 の窓でしか走らず、KYIELD(sti;hlt;cli)
 * では IRQ0 が優先して IRQ1 がほぼ処理されない(QEMU で確認)。
 * なので IRQ1 は PIC でマスクして BIOS に取らせず、ここで全部読む。
 * 配列は US(ThinkPad の JIS 配列の記号は一部ずれる。未対応)。 */
/* JIS(日本語 106/109 キー)配列、スキャンコードセット 1。ThinkPad R40(日本向け)用。
 * 0x29(半角/全角)・0x70(ひらがな)・0x7B/0x79(無変換/変換)は無視。0x73=ろ(\ _)、0x7D=¥(\ | で代用)。 */
static const unsigned char kb_norm[0x80] = {
	0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2D, 0x5E, 0x08, 0x09,
	0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x40, 0x5B, 0x0D, 0x00, 0x61, 0x73,
	0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x3B, 0x3A, 0x00, 0x00, 0x5D, 0x7A, 0x78, 0x63, 0x76,
	0x62, 0x6E, 0x6D, 0x2C, 0x2E, 0x2F, 0x00, 0x2A, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00,
};
static const unsigned char kb_shift[0x80] = {
	0x00, 0x1B, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x00, 0x3D, 0x7E, 0x08, 0x09,
	0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0x60, 0x7B, 0x0D, 0x00, 0x41, 0x53,
	0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x2B, 0x2A, 0x00, 0x00, 0x7D, 0x5A, 0x58, 0x43, 0x56,
	0x42, 0x4E, 0x4D, 0x3C, 0x3E, 0x3F, 0x00, 0x2A, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x00, 0x00,
};

static unsigned kb_pend;          /* 0 = 無し。そうでなければ (ah<<8)|al で al==0 は特殊キー */
static unsigned char kb_shift_on, kb_ctrl_on, kb_ext;

static void kb_poll(void)
{
	unsigned char sc, c;

	while (!kb_pend && (inb(0x64) & 0x01)) {
		sc = inb(0x60);
		if (sc == 0xE0) { kb_ext = 1; continue; }
		if (sc & 0x80) {                         /* break(離した) */
			sc &= 0x7F;
			if (sc == 0x2A || sc == 0x36) kb_shift_on = 0;
			else if (sc == 0x1D) kb_ctrl_on = 0;
			kb_ext = 0;
			continue;
		}
		if (sc == 0x2A || sc == 0x36) { kb_shift_on = 1; continue; }
		if (sc == 0x1D) { kb_ctrl_on = 1; continue; }
		if (kb_ext) {                            /* 矢印: 特殊キー(al=0, ah=スキャン) */
			kb_ext = 0;
			if (sc == 0x48 || sc == 0x50 || sc == 0x4B || sc == 0x4D)
				kb_pend = (unsigned)sc << 8;
			else if (sc == 0x53)                 /* Delete → 0x7F */
				kb_pend = 0x7F;
			continue;
		}
		if (sc >= 0x7E) continue;
		c = (unsigned char)(kb_shift_on ? kb_shift[sc] : kb_norm[sc]);
		if (!c) continue;
		if (kb_ctrl_on) {
			if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 1);
			else if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 1);
			else if (c == ']') c = 0x1D;
			else if (c == '[') c = 0x1B;
		}
		kb_pend = c;
	}
}

static int bios_kbd_ready(void)
{
	kb_poll();
	return kb_pend != 0;
}

int con_rx_ready(void)
{
	if (kbd_buf_idx < kbd_buf_len)
		return 1;
	if (serial_ok && (inb(COM1 + 5) & 0x01))
		return 1;
	return bios_kbd_ready();
}

static unsigned short bios_kbd_get(void)
{
	unsigned k = kb_pend;
	kb_pend = 0;
	return (unsigned short)k;
}

unsigned char con_getc(void)
{
	unsigned char c;

	/* 1. バッファに残っていればそれを返す */
	if (kbd_buf_idx < kbd_buf_len) {
		return kbd_buf[kbd_buf_idx++];
	}
	kbd_buf_len = 0;
	kbd_buf_idx = 0;

	/* 2. COM1 または BIOS キーボードが来るまでポーリング */
	for (;;) {
		if (serial_ok && (inb(COM1 + 5) & 0x01)) {
			c = inb(COM1);
			/* QEMU 脱出ハッチ: Ctrl-](0x1D) */
			if (c == 0x1D) {
				outb(0xF4, 0);
				for (;;)
					__asm__ volatile ("hlt");
			}
			return c;
		}

		if (bios_kbd_ready()) {
			unsigned short ax_val = bios_kbd_get();
			unsigned char al = ax_val & 0xFF;
			unsigned char ah = (ax_val >> 8) & 0xFF;

			if (al != 0) {
				/* 通常文字 */
				return al;
			} else {
				/* 特殊キー (矢印等) -> ESC [ A/B/C/D に変換 */
				unsigned char code = 0;
				if (ah == 0x48) code = 'A'; /* 上 */
				else if (ah == 0x50) code = 'B'; /* 下 */
				else if (ah == 0x4D) code = 'C'; /* 右 */
				else if (ah == 0x4B) code = 'D'; /* 左 */

				if (code != 0) {
					kbd_buf[0] = 0x1B;
					kbd_buf[1] = '[';
					kbd_buf[2] = code;
					kbd_buf_len = 3;
					kbd_buf_idx = 0;
					return kbd_buf[kbd_buf_idx++];
				}
				/* その他の特殊キーは読み捨てて続行 */
			}
		}
	}
}

void con_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			con_putc('\r');
		con_putc(*s++);
	}
}



