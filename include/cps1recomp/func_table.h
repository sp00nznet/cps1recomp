/*
 * func_table.h — Function dispatch table for recompiled code.
 *
 * Every recompiled 68k function is registered at its original address.
 * When recompiled code needs to call a subroutine (JSR/BSR), it calls
 * func_table_call() with the target address, which looks up and invokes
 * the corresponding C function.
 */

#ifndef CPS1RECOMP_FUNC_TABLE_H
#define CPS1RECOMP_FUNC_TABLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cps1_func_t)(void);

int func_table_init(void);
void func_table_shutdown(void);

void func_table_register(uint32_t addr, cps1_func_t func);
void func_table_call(uint32_t addr);
cps1_func_t func_table_lookup(uint32_t addr);
uint32_t func_table_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_FUNC_TABLE_H */
