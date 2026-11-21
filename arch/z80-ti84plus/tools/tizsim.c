/* tizsim - a tiny frontend of libz80e (z80e) that runs a tizix ROM image for the TI-84 Plus.
 *   usage: tizsim [-s seconds] [-r] [-l lcd.txt] rom.bin
 *   stdin  -> the link port (link assist RX)      stdout <- the link port (link assist TX)
 *   -s N   stop after N seconds of emulated time (default: run until stdin ends and the output has been quiet for 1s)
 *   -r     real time (sleep so that 6MHz is about right); without it the emulator runs as fast as it can
 *   -l f   write the LCD contents (96x64, '#' = dark) to f at exit
 * z80e emulates the OS timer of the calculator but not the link cable, so the console is the link assist
 * (the same hardware the calculator uses to talk to a PC). */
#include <z80e/ti/asic.h>
#include <z80e/ti/hardware/link.h>
#include <z80e/ti/hardware/t6a04.h>
#include <z80e/runloop/runloop.h>
#include <z80e/core/cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>

extern void (*z80e_link_tx_hook)(int c);
static long long quiet_since;
static long long cycles;
static unsigned hist[4096];
static void on_tx(int c) { putchar(c); quiet_since = cycles; }

int main(int argc, char **argv)
{
    double secs = -1; int rt = 0; const char *lcdf = NULL; const char *rom = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-r")) rt = 1;
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) lcdf = argv[++i];
        else rom = argv[i];
    }
    if (!rom) { fprintf(stderr, "usage: tizsim [-s sec] [-r] [-l lcd.txt] rom.bin\n"); return 2; }
    FILE *f = fopen(rom, "rb");
    if (!f) { perror(rom); return 1; }
    asic_t *asic = asic_init(TI84p, NULL);
    size_t n = fread(asic->mmu->flash, 1, asic->mmu->settings.flash_pages * 0x4000, f);
    fclose(f);
    (void)n;

    setvbuf(stdout, NULL, _IONBF, 0);
    int in_open = 1;
    z80e_link_tx_hook = on_tx;
    const int slice = 100;
    static unsigned char pend[1 << 20]; int npend = 0, ppos = 0;
    if (!rt) {   /* not real time: take the whole input first, then feed it at a fixed pace */
        ssize_t r;
        while (npend < (int)sizeof pend && (r = read(0, pend + npend, sizeof pend - npend)) > 0) npend += (int)r;
        in_open = 0;
    }
    while (1) {
        runloop_tick_cycles(asic->runloop, slice);
        cycles += slice;
        hist[asic->cpu->registers.PC >> 4]++;
        /* feed one byte every ~1.5ms; after each line (CR) wait until the output has been quiet for 0.2s
         * (the prompt is back), and wait 2s of emulated time before the first byte (the boot) */
        if (ppos < npend && link_recv_ready(asic) && (cycles % 9000) < slice &&
            cycles > 12000000 && cycles - quiet_since > (ppos && pend[ppos - 1] == '\r' ? 1200000 : 60000)) {
            if (link_recv_byte(asic, pend[ppos])) { ppos++; quiet_since = cycles; }
        }
        if ((cycles % 6000) < slice) {   /* every 1ms of emulated time */
            if (in_open && ppos >= npend) {
                fd_set fs; struct timeval tv = {0, 0};
                FD_ZERO(&fs); FD_SET(0, &fs);
                if (select(1, &fs, NULL, NULL, &tv) > 0) {
                    ssize_t r = read(0, pend, 4096);
                    if (r <= 0) in_open = 0; else { npend = (int)r; ppos = 0; }
                }
            }
            if (rt) { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
            if (secs >= 0 && cycles >= (long long)(secs * 6000000)) break;
            if (secs < 0 && !in_open && ppos >= npend && cycles - quiet_since > 6000000) break;
        }
    }
    if (getenv("TIZSIM_HIST")) {
        for (int k = 0; k < 8; k++) { int b = 0; for (int i = 1; i < 4096; i++) if (hist[i] > hist[b]) b = i; fprintf(stderr, "pc %04X-%04X: %u\n", b << 4, (b << 4) + 15, hist[b]); hist[b] = 0; }
    }
    if (getenv("TIZSIM_REGS")) {
        z80registers_t *r = &asic->cpu->registers;
        fprintf(stderr, "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X IFF=%d%d halted=%d\n", r->PC, r->SP, r->AF, r->BC, r->DE, r->HL, r->IX, r->IY, asic->cpu->IFF1, asic->cpu->IFF2, asic->cpu->halted);
    }
    if (lcdf) {
        FILE *o = fopen(lcdf, "w");
        ti_bw_lcd_t *lcd = asic->cpu->devices[0x10].device;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 96; x++) fputc(bw_lcd_read_screen(lcd, x, (y + lcd->Z) % 64) ? '#' : '.', o);
            fputc('\n', o);
        }
        fclose(o);
    }
    return 0;
}
