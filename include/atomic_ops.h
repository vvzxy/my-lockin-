/*
 * File: atomic_ops.h
 */




#ifndef _ATOMIC_OPS_H_INCLUDED_
#define _ATOMIC_OPS_H_INCLUDED_

#include <inttypes.h>

#define COMPILER_BARRIER asm volatile("" ::: "memory")
#ifdef __sparc__
/*
 *  sparc code
 */

#  include <atomic.h>

//test-and-set uint8_t
static inline uint8_t tas_uint8(volatile uint8_t *addr) {
    uint8_t oldval;
    __asm__ __volatile__("ldstub %1,%0"
            : "=r"(oldval), "=m"(*addr)
            : "m"(*addr) : "memory");
    return oldval;
}


static inline unsigned long xchg32(volatile unsigned int *m, unsigned int val)
{
    unsigned long tmp1, tmp2;

    __asm__ __volatile__(
            "       mov             %0, %1\n"
            "1:     lduw            [%4], %2\n"
            "       cas             [%4], %2, %0\n"
            "       cmp             %2, %0\n"
            "       bne,a,pn        %%icc, 1b\n"
            "        mov            %1, %0\n"
            : "=&r" (val), "=&r" (tmp1), "=&r" (tmp2)
            : "0" (val), "r" (m)
            : "cc", "memory");
    return val;
}

static inline unsigned long xchg64(volatile unsigned long *m, unsigned long val)                                                                                                             
{
    unsigned long tmp1, tmp2;

    __asm__ __volatile__(
            "       mov             %0, %1\n"
            "1:     ldx             [%4], %2\n"
            "       casx            [%4], %2, %0\n"
            "       cmp             %2, %0\n"
            "       bne,a,pn        %%xcc, 1b\n"
            "        mov            %1, %0\n"
            : "=&r" (val), "=&r" (tmp1), "=&r" (tmp2)
            : "0" (val), "r" (m)
            : "cc", "memory");
    return val;
}


//Compare-and-swap
#define CAS_PTR(a,b,c) atomic_cas_ptr(a,b,c)
#define CAS_U8(a,b,c) atomic_cas_8(a,b,c)
#define CAS_U16(a,b,c) atomic_cas_16(a,b,c)
#define CAS_U32(a,b,c) atomic_cas_32(a,b,c)
#define CAS_U64(a,b,c) atomic_cas_64(a,b,c)
//Swap
#define SWAP_PTR(a,b) atomic_swap_ptr(a,b)
#define SWAP_U8(a,b) atomic_swap_8(a,b)
#define SWAP_U16(a,b) atomic_swap_16(a,b)
#define SWAP_U32(a,b) xchg32(a,b)
#define SWAP_U64(a,b) atomic_swap_64(a,b)
//Fetch-and-increment
#define FAI_U8(a) (atomic_inc_8_nv(a)-1)
#define FAI_U16(a) (atomic_inc_16_nv(a)-1)
#define FAI_U32(a) (atomic_inc_32_nv(a)-1)
#define FAI_U64(a) (atomic_inc_64_nv(a)-1)
//Fetch-and-decrement
#define FAD_U8(a) (atomic_dec_8_nv(a,)+1)
#define FAD_U16(a) (atomic_dec_16_nv(a)+1)
#define FAD_U32(a) (atomic_dec_32_nv(a)+1)
#define FAD_U64(a) (atomic_dec_64_nv(a)+1)
//Increment-and-fetch
#define IAF_U8(a) atomic_inc_8_nv(a)
#define IAF_U16(a) atomic_inc_16_nv(a)
#define IAF_U32(a) atomic_inc_32_nv(a)
#define IAF_U64(a) atomic_inc_64_nv(a)
//Decrement-and-fetch
#define DAF_U8(a) atomic_dec_8_nv(a)
#define DAF_U16(a) atomic_dec_16_nv(a)
#define DAF_U32(a) atomic_dec_32_nv(a)
#define DAF_U64(a) atomic_dec_64_nv(a)
//Test-and-set
#define TAS_U8(a) tas_uint8(a)
//Memory barrier
#define MEM_BARRIER asm volatile("membar #LoadLoad | #LoadStore | #StoreLoad | #StoreStore"); 
//end of sparc code
#elif defined(__tile__)
/*
 *  Tilera code
 */
#include <arch/atomic.h>
#include <arch/cycle.h>
//atomic operations interface
//Compare-and-swap
#define CAS_PTR(a,b,c) arch_atomic_val_compare_and_exchange(a,b,c)
#define CAS_U8(a,b,c)  arch_atomic_val_compare_and_exchange(a,b,c)
#define CAS_U16(a,b,c) arch_atomic_val_compare_and_exchange(a,b,c)
#define CAS_U32(a,b,c) arch_atomic_val_compare_and_exchange(a,b,c)
#define CAS_U64(a,b,c) arch_atomic_val_compare_and_exchange(a,b,c)
//Swap
#define SWAP_PTR(a,b) arch_atomic_exchange(a,b)
#define SWAP_U8(a,b) arch_atomic_exchange(a,b)
#define SWAP_U16(a,b) arch_atomic_exchange(a,b)
#define SWAP_U32(a,b) arch_atomic_exchange(a,b)
#define SWAP_U64(a,b) arch_atomic_exchange(a,b)
//Fetch-and-increment
#define FAI_U8(a) arch_atomic_increment(a)
#define FAI_U16(a) arch_atomic_increment(a)
#define FAI_U32(a) arch_atomic_increment(a)
#define FAI_U64(a) arch_atomic_increment(a)
//Fetch-and-decrement
#define FAD_U8(a) arch_atomic_decrement(a)
#define FAD_U16(a) arch_atomic_decrement(a)
#define FAD_U32(a) arch_atomic_decrement(a)
#define FAD_U64(a) arch_atomic_decrement(a)
//Increment-and-fetch
#define IAF_U8(a) (arch_atomic_increment(a)+1)
#define IAF_U16(a) (arch_atomic_increment(a)+1)
#define IAF_U32(a) (arch_atomic_increment(a)+1)
#define IAF_U64(a) (arch_atomic_increment(a)+1)
//Decrement-and-fetch
#define DAF_U8(a) (arch_atomic_decrement(a)-1)
#define DAF_U16(a) (arch_atomic_decrement(a)-1)
#define DAF_U32(a) (arch_atomic_decrement(a)-1)
#define DAF_U64(a) (arch_atomic_decrement(a)-1)
//Test-and-set
#define TAS_U8(a) arch_atomic_val_compare_and_exchange(a,0,0xff)
//Memory barrier
#define MEM_BARRIER arch_atomic_full_barrier()
//Relax CPU
//define PAUSE cycle_relax()

//end of tilera code
#else
/*
 *  x86 code
 */

#  include <xmmintrin.h>

//Swap pointers
static inline void* swap_pointer(volatile void* ptr, void *x) {
#  ifdef __i386__
    __asm__ __volatile__("xchgl %0,%1"
            :"=r" ((unsigned) x)
            :"m" (*(volatile unsigned *)ptr), "0" (x)
            :"memory");

    return x;
#  elif defined(__x86_64__)
    __asm__ __volatile__("xchgq %0,%1"
            :"=r" ((unsigned long long) x)
            :"m" (*(volatile long long *)ptr), "0" ((unsigned long long) x)
            :"memory");

    return x;
#  endif
}

//Swap uint64_t
static inline uint64_t swap_uint64(volatile uint64_t* target,  uint64_t x) {
    __asm__ __volatile__("xchgq %0,%1"
            :"=r" ((uint64_t) x)
            :"m" (*(volatile uint64_t *)target), "0" ((uint64_t) x)
            :"memory");

    return x;
}

//Swap uint32_t
static inline uint32_t swap_uint32(volatile uint32_t* target,  uint32_t x) {
    __asm__ __volatile__("xchgl %0,%1"
            :"=r" ((uint32_t) x)
            :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
            :"memory");

    return x;
}

//Swap uint16_t
static inline uint16_t swap_uint16(volatile uint16_t* target,  uint16_t x) {
    __asm__ __volatile__("xchgw %0,%1"
            :"=r" ((uint16_t) x)
            :"m" (*(volatile uint16_t *)target), "0" ((uint16_t) x)
            :"memory");

    return x;
}

//Swap uint8_t
static inline uint8_t swap_uint8(volatile uint8_t* target,  uint8_t x) {
    __asm__ __volatile__("xchgb %0,%1"
            :"=r" ((uint8_t) x)
            :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
            :"memory");

    return x;
}

//test-and-set uint8_t
static inline uint8_t tas_uint8(volatile uint8_t *addr) {
    uint8_t oldval;
    __asm__ __volatile__("xchgb %0,%1"
            : "=q"(oldval), "=m"(*addr)
            : "0"((unsigned char) 0xff), "m"(*addr) : "memory");
    return (uint8_t) oldval;
}

//atomic operations interface
//Compare-and-swap
#define CAS_PTR(a,b,c) __sync_val_compare_and_swap(a,b,c)
#define CAS_U8(a,b,c) __sync_val_compare_and_swap(a,b,c)
#define CAS_U16(a,b,c) __sync_val_compare_and_swap(a,b,c)
#define CAS_U32(a,b,c) __sync_val_compare_and_swap(a,b,c)
#define CAS_U64(a,b,c) __sync_val_compare_and_swap(a,b,c)
//Swap
#define SWAP_PTR(a,b) swap_pointer(a,b)
#define SWAP_U8(a,b) swap_uint8(a,b)
#define SWAP_U16(a,b) swap_uint16(a,b)
#define SWAP_U32(a,b) swap_uint32(a,b)
#define SWAP_U64(a,b) swap_uint64(a,b)
//Fetch-and-increment
#define FAI_U8(a) __sync_fetch_and_add(a,1)
#define FAI_U16(a) __sync_fetch_and_add(a,1)
#define FAI_U32(a) __sync_fetch_and_add(a,1)
#define FAI_U64(a) __sync_fetch_and_add(a,1)
//Fetch-and-decrement
#define FAD_U8(a) __sync_fetch_and_sub(a,1)
#define FAD_U16(a) __sync_fetch_and_sub(a,1)
#define FAD_U32(a) __sync_fetch_and_sub(a,1)
#define FAD_U64(a) __sync_fetch_and_sub(a,1)
//Increment-and-fetch
#define IAF_U8(a) __sync_add_and_fetch(a,1)
#define IAF_U16(a) __sync_add_and_fetch(a,1)
#define IAF_U32(a) __sync_add_and_fetch(a,1)
#define IAF_U64(a) __sync_add_and_fetch(a,1)
//Decrement-and-fetch
#define DAF_U8(a) __sync_sub_and_fetch(a,1)
#define DAF_U16(a) __sync_sub_and_fetch(a,1)
#define DAF_U32(a) __sync_sub_and_fetch(a,1)
#define DAF_U64(a) __sync_sub_and_fetch(a,1)
//Test-and-set
#define TAS_U8(a) tas_uint8(a)
//Memory barrier
#define MEM_BARRIER __sync_synchronize()
//Relax CPU
//#define PAUSE _mm_pause()

/*End of x86 code*/
#endif


#endif



