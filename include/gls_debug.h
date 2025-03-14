/*
 * File: gls_debug.h
 */

#ifndef _GLS_DEBUG_H_
#define _GLS_DEBUG_H_

#define GLS_MAX_NUM_THREDS  64

#define GLS_DEBUG_NONE      0
#define GLS_DEBUG_NORMAL    1
#define GLS_DEBUG_DEADLOCK  2
#define GLS_DEBUG_D_STDOUT  3	/* GLS_DEBUG_DEADLOCK but output in stdout */

#define GLS_DEBUG_LOCK_INIT -3	/* value for lock static initializing */

#define GLS_DEBUG_BACKTRACE 1

#define GLS_DEBUG_OUT       stderr
#if GLS_DEBUG_MODE == GLS_DEBUG_D_STDOUT
#  undef GLS_DEBUG_MODE
#  define GLS_DEBUG_MODE GLS_DEBUG_DEADLOCK
#  undef GLS_DEBUG_OUT
#  define GLS_DEBUG_OUT     stdout
#endif

#if GLS_DEBUG_MODE >= GLS_DEBUG_NORMAL
#  define GLS_DEBUG(x)      x

#  define GLS_DPRINT(format, ...)				\
  {								\
    fprintf(GLS_DEBUG_OUT, "[GLS@ %-25s: %-18s: %-4d]info> ",	\
	    __FILE__, __FUNCTION__, __LINE__);			\
    fprintf(GLS_DEBUG_OUT, format, ##__VA_ARGS__);		\
    fprintf(GLS_DEBUG_OUT, "\n");				\
  }

#  define GLS_WARNING(format, what, lock, ...)				\
  {									\
    fprintf(GLS_DEBUG_OUT, "[GLS@ %-25s: %-18s: %-4d]WARN> %-12s %p -- ", \
	    __FILE__, __FUNCTION__, __LINE__, what, (void*) lock);	\
    fprintf(GLS_DEBUG_OUT, format, ##__VA_ARGS__);			\
    fprintf(GLS_DEBUG_OUT, "\n");					\
    gls_print_backtrace();						\
  }

#define unlikely(x)     __builtin_expect(!!(x), 0)
#define likely(x)     __builtin_expect(!!(x), 1)

/* debug deadlock do */
#  define GLS_DDD(x) x
#  define GLS_DD_CHECK(n_spins)						\
  if (unlikely(++n_spins & ((1LL<<25)-1)) == 0)				\
    {									\
      clht_lp_ddd_check(gls_hashtable);					\
    }					       

#  define GLS_DD_CHECK_FAST(n_spins)					\
  if (unlikely((++n_spins & 3) == 0))					\
    {									\
      clht_lp_ddd_check(gls_hashtable);					\
    }					       

typedef struct gls_waiting
{
  void* addr;
  uint8_t padding[CACHE_LINE_SIZE - sizeof(void*)];
} gls_waiting_t;

extern size_t gls_get_id();
struct clht_lp;
extern struct clht_lp* gls_hashtable;
extern void clht_lp_ddd_check(struct clht_lp* h);

#define gls_get_id_arr() (gls_get_id() - 1)

#else
#  define GLS_DEBUG(x)   
#  define GLS_DPRINT(format, ...)			       
#  define GLS_WARNING(format, ...)			       
#  define GLS_DDD(x)
#  define GLS_DD_CHECK(n_spins)					      
#  define GLS_DD_CHECK_FAST(n_spins)					      
#  define GLS_DDD(x)
#endif

extern void gls_print_backtrace();


#endif /* _GLS_DEBUG_H_ */
