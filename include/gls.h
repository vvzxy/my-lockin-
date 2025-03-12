/*
 * File: gls.h
 */

#ifndef _GLS_H_
#define _GLS_H_

#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

/* #undef  PADDING */
/* #define PADDING 0 */

#include "atomic_ops.h"
#include "glk.h"
#include "mcs_glk_impl.h"
#include "ttas_glk_impl.h"
#include "tas_glk_impl.h"
#include "ticket_glk_impl.h"
#include "mutex_glk_impl.h"

#ifndef GLS_DEBUG_MODE
#  define GLS_DEBUG_MODE      GLS_DEBUG_NONE
#endif

#include "gls_debug.h"

#define DEFAULT_CLHT_SIZE   256
#define GLS_LOCK_CACHE      1

void gls_init(uint32_t num_locks);
void gls_free();
size_t gls_get_id();

void gls_lock_init(void* mem_addr);

void gls_lock(void* mem_addr);
void gls_lock_ttas(void* mem_addr);
void gls_lock_ticket(void* mem_addr);
void gls_lock_mcs(void* mem_addr);
void gls_lock_mutex(void* mem_addr);
void gls_lock_tas(void* mem_addr);
void gls_lock_tas_in(void* mem_addr);

int gls_trylock(void* mem_addr);
int gls_trylock_ttas(void *mem_addr);
int gls_trylock_ticket(void *mem_addr);
int gls_trylock_mcs(void *mem_addr);
int gls_trylock_mutex(void *mem_addr);
int gls_trylock_tas(void* mem_addr);
int gls_trylock_tas_in(void* mem_addr);

void gls_unlock(void* mem_addr);
void gls_unlock_ttas(void* mem_addr);
void gls_unlock_ticket(void* mem_addr);
void gls_unlock_mcs(void* mem_addr);
void gls_unlock_mutex(void* mem_addr);
void gls_unlock_tas(void* mem_addr);
void gls_unlock_tas_in(void* mem_addr);

#endif /* _GLS_H_ */
