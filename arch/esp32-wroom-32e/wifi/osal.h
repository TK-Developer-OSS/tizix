#pragma once
#include <stdint.h>
#include <stddef.h>

#define OS_WAIT_FOREVER 0xFFFFFFFFu

static inline uint32_t irq_off(void) { uint32_t ps; __asm__ volatile ("rsil %0, 3" : "=r"(ps) :: "memory"); return ps; }
static inline void irq_restore(uint32_t ps) { __asm__ volatile ("wsr.ps %0\n\trsync" :: "r"(ps) : "memory"); }

uint64_t os_time_us(void);
uint32_t os_ticks(void);
size_t os_heap_free(void);
int os_in_isr(void);
void os_isr_enter(void);
void os_isr_leave(void);
void os_yield(void);
void *os_current(void);
int os_thread_create(void (*fn)(void *), const char *name, uint32_t stack_size, void *arg, void **handle);
void os_thread_delete(void *h);
int os_wait(int (*cond)(void *), void *arg, uint32_t ticks);
void os_delay(uint32_t ticks);
void os_timer_setfn(void *t, void *fn, void *arg);
void os_timer_arm_ms(void *t, uint32_t ms, int repeat);
void os_timer_disarm(void *t);
