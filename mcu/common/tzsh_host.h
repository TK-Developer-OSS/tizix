#ifndef TZSH_HOST_H
#define TZSH_HOST_H

/* What user/tzsh/tzsh_core.c needs from the host (here: tizix-mcu, constant text in flash). */
#include "mcu.h"
#include <string.h>

#define TZ_P(s)             P(s)
#define tz_getb(p)          mgetb_P(p)          /* one byte of constant memory */
#define tz_strcmp_P         mstrcmp_P           /* (RAM string, constant string) */
#define tz_strlen_P         mstrlen_P
#define tz_puts_P           mputs_P             /* constant string to the console */
#define tz_putu             mputu
#define tz_run              cmd_run             /* run one command; returns its status */
#define tz_run_to           cmd_run_to          /* ... with its output sent to a /dev node */
#define tz_dev_read         dev_read            /* the value of a /dev node as text; 0 = ok */
#define tz_pending()        hal_event_take()    /* an interrupt event is waiting (and clear it) */
#define tz_intr()           (hal_poll() == 0x03)    /* Ctrl-C */

#endif
