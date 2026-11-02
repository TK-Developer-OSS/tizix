#include "plat.h"
#include "../../../src/loader.h"
#include "../../../src/ff.h"

unsigned char plat_need(FIL *fp) {
    return 1;
}

unsigned char plat_load(FIL *fp, unsigned char n, unsigned char k, struct kimage *im) {
    return 0;
}

unsigned long plat_ctx(const struct kimage *im, unsigned long argc, unsigned long argv) {
    return 0;
}

void mbox_slot_reset(unsigned char n) {
}
