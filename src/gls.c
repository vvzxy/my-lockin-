/*
 * File: gls.c
 */

#include "gls.h"
#include "clht_lock_pointer.h"


clht_lp_t* gls_hashtable;

#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK//死锁调试模式相关代码
// static size_t __gls_ids = 0;/用来为每个线程分配唯一 ID 的全局计数器
static __thread size_t __gls_id = 0;//每个线程独有的 ID，用于标识当前线程

inline size_t
gls_get_id()
{
  if (unlikely(__gls_id == 0))
    {
      __gls_id = __sync_add_and_fetch(&__gls_ids, 1);
    }
  return __gls_id;
}//gls_get_id 函数为当前线程分配一个唯一的 ID，使用原子操作 __sync_add_and_fetch 增加全局 ID 计数器。ID 初始化后会缓存在线程局部变量 __gls_id 中，避免多次调用,不使用pid.

#  define GLS_DD_SET_OWNER(mem_addr)					\
  clht_lp_ddd_waiting_unset(gls_hashtable, mem_addr);			\
  if (unlikely(!clht_lp_set_owner(gls_hashtable->ht, (clht_lp_addr_t) mem_addr, gls_get_id())))	\
    {									\
      GLS_WARNING("could not set", "SET-OWNER", mem_addr);		\
    }
// / 宏定义 GLS_DD_SET_OWNER，用于在死锁调试模式下设置锁的所有者。函数 clht_lp_set_owner 设置哈希表中对应内存地址的锁定线程 ID。如果设置失败，会发出警告。
#  define GLS_DD_SET_OWNER_IF(ret, mem_addr)				\
  clht_lp_ddd_waiting_unset(gls_hashtable, mem_addr);			\
  if ((ret == 0) &&							\
      unlikely(!clht_lp_set_owner(gls_hashtable->ht, (clht_lp_addr_t) mem_addr, gls_get_id()))) \
    {									\
      GLS_WARNING("could not set", "SET-OWNER", mem_addr);		\
    }//宏定义 GLS_DD_SET_OWNER_IF 用于有条件地设置锁的所有者。如果 ret == 0（加锁成功），则设置该内存地址的锁拥有者。

#  undef GLS_LOCK_CACHE
#  define GLS_LOCK_CACHE 0

#else /* ! GLS_DEBUG_DEADLOCK*/
#  define GLS_DD_SET_OWNER(mem_addr)
#  define GLS_DD_SET_OWNER_IF(res, mem_addr)
#endif	/* GLS_DEBUG_DEADLOCK */

//缓存锁相关代码,
#if GLS_LOCK_CACHE == 1
struct gls_lock_cache
{
  void* lock;
  void* addr;
};
static __thread struct gls_lock_cache __thread_lock_cache = { .lock = NULL, .addr = NULL };
#endif//如果启用了锁缓存功能（GLS_LOCK_CACHE == 1），定义一个结构体 gls_lock_cache，用于缓存最近的锁地址。每个线程有自己独立的缓存（__thread_lock_cache），存储了锁和内存地址。

static inline void
gls_lock_cache_set(void* lock, void* address)
{
#if GLS_LOCK_CACHE == 1
  __thread_lock_cache.lock = lock;
  __thread_lock_cache.addr = address;
#endif
}//函数用于设置当前线程的锁缓存，将锁和对应的内存地址存储在缓存中.

#if GLS_LOCK_CACHE == 1
#  define GLS_LOCK_CACHE_GET(mem_addr)			\
  if (likely(__thread_lock_cache.lock == mem_addr))	\
    {							\
      return __thread_lock_cache.addr;			\
    }
// #else/宏定义 GLS_LOCK_CACHE_GET，用于检查当前线程的锁缓存。如果缓存中已经有与 mem_addr 对应的锁，则直接返回缓存中的锁地址，避免重复查找。
#  define GLS_LOCK_CACHE_GET(mem_addr)		       
#endif

/* *********************************************************************************************** */
/* help functions */
/* *********************************************************************************************** */

static inline void*
gls_lock_get_put(clht_lp_t* gls_hashtable, void* mem_addr, const int lock_type)//需要锁的类型
{
  GLS_LOCK_CACHE_GET(mem_addr);

  void* lock_addr = (void*) clht_lp_put_type(gls_hashtable, (clht_lp_addr_t) mem_addr, lock_type);
#if GLS_LOCK_CACHE == 1
  gls_lock_cache_set(mem_addr, lock_addr);
#endif
  return lock_addr;
}//根据内存地址 mem_addr 获取或者创建一个锁条目，并返回对应的锁地址。使用 clht_lp_put_type 将内存地址和锁类型存储在哈希表中。锁地址可以缓存（如果启用了锁缓存），返回的是锁的实际值

static inline void*
gls_lock_get_put_in(clht_lp_t* gls_hashtable, void* mem_addr)//不需要锁的类型
{
  void* lock_addr = (void*) clht_lp_put_in(gls_hashtable, (clht_lp_addr_t) mem_addr);
#if GLS_LOCK_CACHE == 1
  gls_lock_cache_set(mem_addr, lock_addr);
#endif
  return lock_addr;
}//返回锁地址的地址

static inline void*
gls_lock_get_get(clht_lp_t* gls_hashtable, void* mem_addr, int lock_type, const char* from)
{
  GLS_LOCK_CACHE_GET(mem_addr);

  void* lock_addr = (void*) clht_lp_get(gls_hashtable->ht, (clht_lp_addr_t) mem_addr);
  GLS_DEBUG
    (
     if (unlikely(lock_addr == NULL))
       {
	 GLS_WARNING("not initialized in %s", "UNLOCK", mem_addr, from);
	 lock_addr = (void*) gls_lock_get_put(gls_hashtable, mem_addr, lock_type);
       }
     );

  return lock_addr;
}//根据内存地址获取锁地址，如果哈希表中没有找到锁地址，则调用 gls_lock_get_put 创建新锁。函数还支持调试模式下的警告输出，返回锁的实际值

static inline void*
gls_lock_get_get_in(clht_lp_t* gls_hashtable, void* mem_addr, const char* from)
{
  void* lock_addr = (void*) clht_lp_get_in(gls_hashtable->ht, (clht_lp_addr_t) mem_addr);
  GLS_DEBUG
    (
     if (unlikely(lock_addr == NULL))
       {
	 GLS_WARNING("not initialized in %s", "UNLOCK", mem_addr, from);
	 lock_addr = (void*) gls_lock_get_put_in(gls_hashtable, mem_addr);
       }
     );

  return lock_addr;
}//返回锁地址的地址



/* *********************************************************************************************** */
/* init functions */
/* *********************************************************************************************** */

static volatile int gls_initialized = 0;
static volatile int gls_initializing = 0;
// 定义两个全局变量 gls_initialized 和 gls_initializing，用于标记 GLS 是否已经初始化和是否正在初始化。

/* unlikely was hurting performance at some test :-( */
#define GLS_INIT_ONCE()				\
  if (likely(gls_initialized == 0))		\
    {						\
      gls_init(DEFAULT_CLHT_SIZE);		\
    }//这个宏用于确保 GLS 的初始化只进行一次，典型的 "单例模式"（Singleton pattern）

void gls_init(uint32_t num_locks) {
  if (__sync_val_compare_and_swap(&gls_initializing, 0, 1) == 1) {//确保在多线程环境中只有一个线程进行初始化
  //如果函数返回值为 1，意味着其他线程已经开始初始化，此时当前线程进入 while 循环等待初始化完成
    while (gls_initialized == 0)
      {
	PAUSE_IN();
      }
    return;
  }//如果有其他线程正在进行初始化，当前线程会在这个循环中等待，直到 gls_initialized 变为 1，表示初始化完成。

  assert(gls_initialized == 0);

  uint32_t num_buckets = num_locks == 0
    ? DEFAULT_CLHT_SIZE
    : num_locks; // (num_locks + ENTRIES_PER_BUCKET - 1) /  ENTRIES_PER_BUCKET;
    //根据传入的 num_locks 参数，确定哈希表的大小 num_buckets。如果 num_locks == 0，则使用默认值 DEFAULT_CLHT_SIZE。
  gls_hashtable = clht_lp_create(num_buckets);//创建一个包含 num_buckets 个桶的哈希表，用于存储 GLS 中的锁条目。
  assert(gls_hashtable != NULL);//确保哈希表创建成功。
  gls_initialized = 1;
  GLS_DPRINT("Initialized");
}//gls_init 函数初始化 GLS 系统，分配用于存储锁的哈希表。它使用原子操作确保初始化只发生一次，如果其他线程在初始化时调用此函数，则会等待初始化完成。

void gls_free() 
{
  if (gls_initialized)
    {
      clht_lp_destroy(gls_hashtable->ht);
    }
}//释放 GLS 所使用的资源，特别是哈希表。该函数在系统不再需要 GLS 时调用，用于清理并释放已分配的资源。


void gls_lock_init(void* mem_addr) 
{
  GLS_INIT_ONCE();
  if (clht_lp_put_init(gls_hashtable, (clht_lp_addr_t) mem_addr))
    {
      GLS_WARNING("Double initialization", "LOCK-INIT", mem_addr);
    }
}//gls_lock_init() 函数根据给定的内存地址 mem_addr 初始化该内存地址的锁。如果该地址已经被初始化，则发出警告。
// /试图在哈希表中为 mem_addr 分配并初始化锁,

/* *********************************************************************************************** */
/* lock functions */
/* *********************************************************************************************** */

inline void gls_lock(void* mem_addr) 
{
  GLS_INIT_ONCE();//// 确保 GLS 系统只初始化一次

  glk_t *lock = 
    (glk_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE);//自适应
  glk_lock(lock);// 对锁执行加锁操作
  GLS_DD_SET_OWNER(mem_addr);
}

// inline void gls_lock_ttas(void* mem_addr) 
// {
//   GLS_INIT_ONCE();

//   ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TTAS);
//   ttas_lock_lock(lock);
//   GLS_DD_SET_OWNER(mem_addr);
// }

// inline void gls_lock_tas(void* mem_addr) 
// {
//   GLS_INIT_ONCE();

//   tas_lock_t *lock = (tas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TAS);
//   tas_lock_lock(lock);
//   GLS_DD_SET_OWNER(mem_addr);
// }

inline void gls_lock_ticket(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock = (ticket_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TICKET);
  ticket_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_mcs(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MCS);
  mcs_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_mutex(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock = (mutex_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MUTEX);
  mutex_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}



/* *********************************************************************************************** */
/* trylock functions */
/* *********************************************************************************************** */

inline int gls_trylock(void *mem_addr) 
{
  GLS_INIT_ONCE();

  glk_t *lock = 
    (glk_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE);
  const int res = glk_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

// inline int gls_trylock_ttas(void *mem_addr) 
// {
//   GLS_INIT_ONCE();

//   ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TTAS);
//   const int res = ttas_lock_trylock(lock);
//   GLS_DD_SET_OWNER_IF(res, mem_addr);
//   return res;
// }

// inline int gls_trylock_tas(void *mem_addr) 
// {
//   GLS_INIT_ONCE();

//   tas_lock_t *lock = (tas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TAS);
//   const int res = tas_lock_trylock(lock);
//   GLS_DD_SET_OWNER_IF(res, mem_addr);
//   return res;
// }

inline int gls_trylock_ticket(void *mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock = (ticket_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TICKET);
  const int res = ticket_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_mcs(void *mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MCS);
  const int res = mcs_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_mutex(void *mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock = (mutex_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MUTEX);
  const int res = mutex_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}




/* *********************************************************************************************** */
/* unlock functions */
/* *********************************************************************************************** */

inline void gls_unlock(void* mem_addr) 
{
  GLS_INIT_ONCE();

  glk_t *lock = 
    (glk_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(glk_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
	 GLS_WARNING("skip cause LOAD lock could break", "UNLOCK", mem_addr);
	 return;
       }
     );
  glk_unlock(lock);
}

// inline void gls_unlock_ttas(void* mem_addr) 
// {
//   GLS_INIT_ONCE();

//   ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TTAS, __FUNCTION__);
//   GLS_DEBUG
//     (
//      if (unlikely(ttas_lock_is_free(lock)))
//        {
// 	 GLS_WARNING("already free", "UNLOCK", mem_addr);
//        }
//      );  
//   ttas_lock_unlock(lock);
// }

// inline void gls_unlock_tas(void* mem_addr) 
// {
//   GLS_INIT_ONCE();

//   tas_lock_t *lock = (tas_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TAS, __FUNCTION__);
//   GLS_DEBUG
//     (
//      if (unlikely(tas_lock_is_free(lock)))
//        {
// 	 GLS_WARNING("already free", "UNLOCK", mem_addr);
//        }
//      );  
//   tas_lock_unlock(lock);
// }

inline void gls_unlock_ticket(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock =
    (ticket_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TICKET, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(ticket_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  ticket_lock_unlock(lock);
}

inline void gls_unlock_mcs(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_MCS, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(mcs_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
	 GLS_WARNING("skip cause MCS lock could break", "UNLOCK", mem_addr);
	 return;
       }
     );  
  mcs_lock_unlock(lock);
}

inline void gls_unlock_mutex(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock =
    (mutex_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_MUTEX, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(mutex_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  mutex_unlock(lock);
}

/* ******************************************************************************** */
/* lock inlined fucntions */
/* ******************************************************************************** */

typedef  volatile clht_lp_val_t gls_tas_t;

#define GLS_TAS_FREE   0
#define GLS_TAS_LOCKED 1
#define GLS_CAS(a, b, c) __sync_val_compare_and_swap(a, b, c)

inline void gls_lock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();

  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_put_in(gls_hashtable, mem_addr);
  if (likely(GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) == GLS_TAS_FREE))
    {
      GLS_DD_SET_OWNER(mem_addr);
      return;
    }

  do
    {
      asm volatile ("pause");
    }
  while (GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) != GLS_TAS_FREE);
  GLS_DD_SET_OWNER(mem_addr);
}

inline int gls_trylock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();

  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_put_in(gls_hashtable, mem_addr);
  int res = (GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) != GLS_TAS_FREE);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline void gls_unlock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();
  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_get_in(gls_hashtable, mem_addr, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(*lock == GLS_TAS_FREE))
       {
     	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );

  asm volatile ("" ::: "memory");
  *lock = GLS_TAS_FREE;
  asm volatile ("" ::: "memory");
}


/* help */

#include <execinfo.h>

void
gls_print_backtrace()
{
#if GLS_DEBUG_BACKTRACE == 1
  void* trace[16];
  char** messages = (char**) NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  printf("   [BACKTRACE] Execution path:\n");
  for (i = 0; i < trace_size; i++)
    {
      printf("   [BACKTRACE] #%d %s\n", i - 2, messages[i]);
      /* find first occurence of '(' or ' ' in message[i] and assume
       * everything before that is the file name. (Don't go beyond 0 though
       * (string terminator)*/
      int p = 0;
      while(messages[i][p] != '(' && messages[i][p] != ' '
	    && messages[i][p] != 0)
	++p;

      char syscom[256];
      sprintf(syscom,"addr2line %p -e %.*s", trace[i], p, messages[i]);
      //last parameter is the file name of the symbol
      UNUSED int r = system(syscom);
    }
#endif
}
