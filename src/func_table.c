/*
 * func_table.c — Function dispatch table implementation.
 */

#include <cps1recomp/func_table.h>
#include <cps1recomp/debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct func_entry {
    uint32_t addr;
    cps1_func_t func;
    struct func_entry *next;
} func_entry_t;

#define FUNC_TABLE_BUCKETS 4096

static func_entry_t *s_buckets[FUNC_TABLE_BUCKETS];
static uint32_t s_count = 0;

static inline uint32_t addr_hash(uint32_t addr) {
    return (addr >> 1) & (FUNC_TABLE_BUCKETS - 1);
}

int func_table_init(void) {
    memset(s_buckets, 0, sizeof(s_buckets));
    s_count = 0;
    return 0;
}

void func_table_shutdown(void) {
    for (int i = 0; i < FUNC_TABLE_BUCKETS; i++) {
        func_entry_t *e = s_buckets[i];
        while (e) {
            func_entry_t *next = e->next;
            free(e);
            e = next;
        }
        s_buckets[i] = NULL;
    }
    s_count = 0;
}

void func_table_register(uint32_t addr, cps1_func_t func) {
    uint32_t bucket = addr_hash(addr);
    func_entry_t *e = s_buckets[bucket];
    while (e) {
        if (e->addr == addr) { e->func = func; return; }
        e = e->next;
    }
    e = (func_entry_t *)malloc(sizeof(func_entry_t));
    if (!e) { fprintf(stderr, "[func_table] Alloc failed for $%06X\n", addr); return; }
    e->addr = addr;
    e->func = func;
    e->next = s_buckets[bucket];
    s_buckets[bucket] = e;
    s_count++;
}

void func_table_call(uint32_t addr) {
    cps1_func_t func = func_table_lookup(addr);
    if (func) {
        debug_trace_call(addr, NULL);
        func();
    } else {
        debug_log("[func_table] WARNING: No function at $%06X\n", addr);
    }
}

cps1_func_t func_table_lookup(uint32_t addr) {
    uint32_t bucket = addr_hash(addr);
    func_entry_t *e = s_buckets[bucket];
    while (e) {
        if (e->addr == addr) return e->func;
        e = e->next;
    }
    return NULL;
}

uint32_t func_table_count(void) {
    return s_count;
}
