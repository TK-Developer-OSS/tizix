#ifndef TZSH_CORE_H
#define TZSH_CORE_H

/* tzsh core for small targets. script is NUL-terminated text in the host's constant memory
 * (flash on a Harvard chip). Returns the exit status of the script. */
int tzsh_run(const char *script);

#endif
