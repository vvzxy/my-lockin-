#include "glk.h"
#include <sys/time.h>
#include <string.h>
//各种锁的加锁解锁操作

// Background task for multiprogramming detection
static volatile ALIGNED(CACHE_LINE_SIZE) int multiprogramming = 0;

#if GLK_THREAD_CACHE == 1
struct ALIGNED(CACHE_LINE_SIZE) glk_thread_cache
{
  glk_t* lock;
  uint32_t lock_type;
};
static __thread struct glk_thread_cache __thread_cache = { NULL, 0 };
#endif

static inline void
glk_thread_cache_set(glk_t* lock, uint32_t type)
{
#if GLK_THREAD_CACHE == 1
  __thread_cache.lock = lock;
  __thread_cache.lock_type = type;
#endif
}

static inline  int
glk_thread_cache_get_type(glk_t* lock)
{
#if GLK_THREAD_CACHE == 1
  if (likely(__thread_cache.lock == lock))
    {
      return __thread_cache.lock_type;
    }
  else
    {
      return lock->lock_type;
    }
#else
  return lock->lock_type;
#endif
}

static int glk_mcs_lock_lock(glk_t* lock);
static inline int gls_adaptinve_mcs_lock_queue_length(glk_mcs_lock_t* lock);
static int glk_ticket_lock_lock(glk_t* gl);
static inline int glk_ticket_lock_unlock(glk_ticket_lock_t* lock);
static inline int glk_ticket_lock_trylock(glk_ticket_lock_t* lock);
static inline int glk_ticket_lock_init(glk_ticket_lock_t* the_lock, const pthread_mutexattr_t* a);


static inline int glk_mutex_lock(glk_mutex_lock_t* lock);
static inline int glk_mutex_unlock(glk_mutex_lock_t* m);
static inline int glk_mutex_lock_trylock(glk_mutex_lock_t* m);
static inline int glk_mutex_init(glk_mutex_lock_t* m);


void*
glk_mp_check(void *arg)
{  
  FILE *f;
  float lavg[3];
  int nr_running, nr_tot, nr_what;
  int nr_hw_ctx = sysconf(_SC_NPROCESSORS_ONLN);
  const char* lafile = "/proc/loadavg";
  periodic_data_t *data = (periodic_data_t *) arg;

  unsigned int sec = data->period_us / 1000000;
  unsigned long ns = (data->period_us - (sec * 1000000)) * 1000;
  struct timespec timeout;
  timeout.tv_sec = sec;
  timeout.tv_nsec = ns;

  /* unpin the thread */
  int cpu = -1;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
    {
      fflush(stdout);
    }

  struct timespec time_start, time_stop;
  size_t ms_start = 0, n_sec = 0, n_adap = 0, n_mp_run = 0;
  int time_start_is_set = 0;

  size_t mp_fixed_times = 0;

  while (1)
    {
      n_mp_run++;
      if ((f = fopen(lafile, "r")) == NULL)
	{
	  fprintf(stderr, "Error opening %s file\n", lafile);
	  return 0;
	}
      int n = fscanf(f, "%f %f %f %d/%d %d", &lavg[0], &lavg[1], &lavg[2], &nr_running, &nr_tot, &nr_what);
      if (n != 6)
	{
	  fprintf(stderr, "Error reading %s\n", lafile);
	  continue;
	}

      nr_running--;
      if (nr_running > nr_hw_ctx + GLK_MP_CHECK_THRESHOLD_HIGH)
	{
	  data->num_zeroes_encountered = 0;
	  if (!multiprogramming)
	    {
	      if ((data->num_zeroes_required <<= GLK_MP_NUM_ZERO_SHIFT) > GLK_MP_NUM_ZERO_MAX)
		{
		  data->num_zeroes_required = GLK_MP_NUM_ZERO_MAX;
		}
	      multiprogramming = 1;
	      n_adap++;
	      time_t tm = time(NULL);
	      const char* tms = ctime(&tm);
	      __attribute__((unused)) int len = strlen(tms);
	      glk_dlog("[.BACKGRND] (%.*s) switching TO multiprogramming: (%-3d)\n",
	      		 len - 1, tms, nr_running - 1);
	      timeout.tv_nsec <<= GLK_MP_SLEEP_SHIFT;
	    }
	}
      else if (nr_running < (nr_hw_ctx - GLK_MP_CHECK_THRESHOLD_LOW))
	{
	  if (multiprogramming)
	    {
	      data->num_zeroes_encountered++;
	      if (data->num_zeroes_encountered >= data->num_zeroes_required)
		{
		  multiprogramming = 0;
		  n_adap++;
		  time_t tm = time(NULL);
		  const char* tms = ctime(&tm);
		  __attribute__((unused)) int len = strlen(tms);
		  glk_dlog("[.BACKGRND] (%.*s) switching TO spinning        : (%-3d) (limit %d)\n",
		  	     len - 1, tms, nr_running - 1, data->num_zeroes_required);
		  timeout.tv_nsec >>= GLK_MP_SLEEP_SHIFT;
		}
	    }
	  else
	    {
	      if (--data->num_zeroes_required < GLK_MP_NUM_ZERO_REQ)
		{
		  data->num_zeroes_required = GLK_MP_NUM_ZERO_REQ;
		}
	    }
	}

      fclose(f);
      

      if (time_start_is_set == 0)
      	{
	  clock_gettime(CLOCK_REALTIME, &time_start);
	  time_start_is_set = 1;
	  ms_start = (time_start.tv_sec * 1000) + (time_start.tv_nsec / 1e6);
	}

      nanosleep(&timeout, NULL);

      clock_gettime(CLOCK_REALTIME, &time_stop);
      size_t ms_stop = (time_stop.tv_sec * 1000) + (time_stop.tv_nsec / 1e6);
      size_t ms_diff = ms_stop - ms_start;
      if (ms_diff >= 1000)
	{
	  n_sec++;
	  time_start_is_set = 0;
	  double sec = ms_diff / 1000.0;
	  size_t adap_per_sec = n_adap / sec;
	  glk_dlog("[.BACKGRND] [%-4zu seconds / %-4zu ms]: adaps/s = %-3zu | mp_checks/s = %3.0f\n",
		     n_sec, ms_diff, adap_per_sec, n_mp_run / sec);
	  n_adap = 0;
	  n_mp_run = 0;
	  if (adap_per_sec > GLK_MP_MAX_ADAP_PER_SEC)
	    {
	      multiprogramming = 1;
	      size_t sleep_shift = mp_fixed_times++;
	      if (sleep_shift > GLK_MP_FIXED_SLEEP_MAX_LOG)
		{
		  sleep_shift = GLK_MP_FIXED_SLEEP_MAX_LOG; 
		}
	      size_t mp_fixed_sleep_s = 1LL << sleep_shift;
	      struct timespec mp_fixed_sleep = { .tv_sec = mp_fixed_sleep_s, .tv_nsec = 0};
	      glk_dlog("[.BACKGRND] fixing mp for %zu seconds\n", mp_fixed_sleep_s);
	      n_sec += mp_fixed_sleep_s;
	      nanosleep(&mp_fixed_sleep, NULL);
	    }
	  else if (adap_per_sec <= GLK_MP_LOW_ADAP_PER_SEC)
	    {
	      mp_fixed_times >>= 1;
	    }
	}
    }
  return NULL;
}

static volatile int adaptive_lock_global_initialized = 0;
static volatile int adaptive_lock_global_initializing = 0;

inline int
gls_is_multiprogramming()
{
  return multiprogramming;
}

void adaptive_lock_global_init()
{
  if (__sync_val_compare_and_swap(&adaptive_lock_global_initializing, 0, 1) == 1)
    {
      return;
    }

  assert(adaptive_lock_global_initialized == 0);
#if GLK_DO_ADAP == 1
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  periodic_data_t* data = (periodic_data_t*) malloc(sizeof(periodic_data_t));
  assert(data != NULL);
  data->num_zeroes_required = GLK_MP_NUM_ZERO_REQ;
  data->num_zeroes_encountered = 0;

  data->period_us = GLK_MP_CHECK_PERIOD_US;
  if (pthread_create(&thread, &attr, glk_mp_check, (void*) data) != 0)
    {
      fprintf(stderr, "Error creating thread\n");
      exit(1);
    }
  pthread_attr_destroy(&attr);
#endif

  adaptive_lock_global_initialized = 1;
}


inline void
unlock_lock(glk_t* lock, const int type)
{
  switch(type)
    {
    case TICKET_LOCK:
      glk_ticket_lock_unlock(&lock->ticket_lock);
      break;
    case MCS_LOCK:
      glk_mcs_lock_unlock(&lock->mcs_lock);
      break;
    case MUTEX_LOCK:
      glk_mutex_unlock(&lock->mutex_lock);
      break;
    }
}

inline int
glk_unlock(glk_t* lock) 
{
  const int current_lock_type = glk_thread_cache_get_type(lock);
  switch(current_lock_type)
    {
    case TICKET_LOCK:
      return glk_ticket_lock_unlock(&lock->ticket_lock);
    case MCS_LOCK:
      return glk_mcs_lock_unlock(&lock->mcs_lock);
    case MUTEX_LOCK:
      return glk_mutex_unlock(&lock->mutex_lock);
    }
  return 0;
}

/* returns 0 on success */
inline int
glk_trylock(glk_t* lock)
{
  do
    {
      const int current_lock_type = lock->lock_type;
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  if (glk_ticket_lock_trylock(&lock->ticket_lock))
	    {
	      return 1;
	    }
	  break;
	case MCS_LOCK:
	  if (glk_mcs_lock_trylock(&lock->mcs_lock))
	    {
	      return 1;
	    }
	  break;
	case MUTEX_LOCK:
	  if (glk_mutex_lock_trylock(&lock->mutex_lock))
	    {
	      return 1;
	    }
	  break;
	}

      if (unlikely(lock->lock_type == current_lock_type))
	{
	  break;
	}
      else
	{
	  unlock_lock(lock, current_lock_type);
	}
    }
  while (1);
  return 0;
}



static inline void
glk_ticket_adap(glk_t* lock, const uint32_t ticket)
{
  if (GLK_MUST_TRY_ADAPT(ticket))
    {
      if (unlikely(adaptive_lock_global_initialized == 0))
	{
	  adaptive_lock_global_init();
	}

#if GLK_MP_LOW_CONTENTION_TICKET == 1
	  const int do_it = 0;
#else
	  const int do_it = 1;
#endif

      if (do_it && unlikely(multiprogramming))
	{
	  glk_dlog("[%p] %-7s ---> %-7s\n", lock, "TICKET", "MUTEX");
	  lock->lock_type = MUTEX_LOCK;
	}
      else
	{
	  const uint32_t queue_total_local = lock->queue_total;
	  const double ratio = ((double) queue_total_local) / GLK_SAMPLE_NUM;
	  if (unlikely(ratio >= GLK_CONTENTION_RATIO_HIGH))
	    {
#if GLK_MP_LOW_CONTENTION_TICKET == 1
	      /* move away from tas iff there is multiprogramming */
	      if (unlikely(multiprogramming))
		{
		  glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
			     lock, "TICKET", "MUTEX", lock->queue_total, GLK_SAMPLE_NUM, ratio);
		  lock->lock_type = MUTEX_LOCK;
		  return;
		}
#endif

	      glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
				lock, "TICKET", "MCS", lock->queue_total, GLK_SAMPLE_NUM, ratio);
	      lock->queue_total = queue_total_local >> GLK_MEASURE_SHIFT;
	      lock->num_acquired = GLK_NUM_ACQ_INIT;
	      lock->lock_type = MCS_LOCK;
	    }
	  else 
	    {
	      lock->queue_total = 0;
	      lock->num_acquired = GLK_NUM_ACQ_INIT;
	    }
	}
    }
}

static inline void
glk_mcs_adap(glk_t* lock, const int num_acq)
{
  if (GLK_MUST_UPDATE_QUEUE_LENGTH(num_acq))
    {

      const int len = gls_adaptinve_mcs_lock_queue_length(&lock->mcs_lock);
      const int queue_total_local = __sync_add_and_fetch(&lock->queue_total, len);

      if (GLK_MUST_TRY_ADAPT(num_acq))
	{
	  if (unlikely(multiprogramming))
	    {
	      glk_dlog("[%p] %-7s ---> %-7s\n", lock, "MCS", "MUTEX");
	      lock->lock_type = MUTEX_LOCK;
	    }
	  else
	    {
	      const double ratio = ((double) queue_total_local) / GLK_SAMPLE_NUM;
	      if (unlikely(ratio < GLK_CONTENTION_RATIO_LOW))
		{
		  glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
				    lock, "MCS", "TICKET", lock->queue_total, GLK_SAMPLE_NUM, ratio);
		  lock->queue_total = 0;
		  lock->num_acquired = GLK_NUM_ACQ_INIT;
		  lock->lock_type = TICKET_LOCK;
		}
	      else 
		{
		  lock->queue_total = queue_total_local >> GLK_MEASURE_SHIFT;
		  lock->num_acquired = GLK_NUM_ACQ_INIT;
		}
	    }
	}
    }
}


static inline void
glk_mutex_adap(glk_t* lock)
{
#if GLK_DO_ADAP == 1
  if (unlikely(!multiprogramming))
    {
      if (likely(lock->lock_type == MUTEX_LOCK))
	{
	  lock->queue_total = 0;
	  lock->num_acquired = 0;
	  lock->lock_type = GLK_MP_TO_LOCK;
	  glk_dlog("[%p] %-7s ---> %-7s\n", lock, "MUTEX",
		     (GLK_MP_TO_LOCK == MCS_LOCK) ? "MCS" : "TICKET");
	}
    }
#endif
}

inline int
glk_lock(glk_t* lock) 
{
  int ret = 0;
  do
    {
      const int current_lock_type = lock->lock_type;// 获取当前锁的类型
      glk_thread_cache_set(lock, current_lock_type);// 设置线程缓存，表示该线程正在使用该锁
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  {
	    const uint32_t ticket  = glk_ticket_lock_lock(lock);
	    glk_ticket_adap(lock, ticket);
	  }
	  break;
	case MCS_LOCK:
	  {
	    int num_acq = glk_mcs_lock_lock(lock);
	    glk_mcs_adap(lock, num_acq);
	  }
	  break;
	case MUTEX_LOCK:
	  {
	    glk_mutex_lock(&lock->mutex_lock);
	    glk_mutex_adap(lock);
	  }
	  break;
	}

      if (likely(lock->lock_type == current_lock_type))
	{
	  break;
	}
      else
	{
	  unlock_lock(lock, current_lock_type);
	}
    }
  while (1);

  return ret;
}

int glk_destroy(glk_t *lock) {
  return 0;
}

int
glk_init(glk_t *lock, const pthread_mutexattr_t* a)
{
  if (unlikely(adaptive_lock_global_initialized == 0))
    {
      adaptive_lock_global_init();
    }

  if (gls_is_multiprogramming())
    {
      lock->lock_type = GLK_INIT_LOCK_TYPE_MP;
    }
  else
    {
      lock->lock_type = GLK_INIT_LOCK_TYPE;
    }

  glk_ticket_lock_init(&lock->ticket_lock, a);
  glk_mcs_lock_init(&lock->mcs_lock, (pthread_mutexattr_t*) a);
  glk_mutex_init(&lock->mutex_lock);
  lock->num_acquired = 0;
  lock->queue_total = 0;

  /* asm volatile ("mfence"); */
  return 0;
}

/* ******************************************************************************** */
/* lock implementations */
/* ******************************************************************************** */

/* **************************************** */
/* MCS */
/* **************************************** */

GLK_MCS_LOCAL_DATA;

int 
glk_mcs_lock_init(glk_mcs_lock_t* lock, pthread_mutexattr_t* a)
{
  lock->next = NULL;
  return 0;
}//初始化，将锁的 next 字段设为 NULL，表示当前锁没有线程在等待。这是锁的初始状态，后续线程会通过将自己添加到 next 中加入队列

//每个线程都有一个自己的本地锁状态，通过 glk_mcs_get_local_lock 函数，线程可以获取自己专属的锁节点，并将其加入到 MCS 锁队列中
inline glk_mcs_lock_t*
glk_mcs_get_local_lock(glk_mcs_lock_t* head)//获取局部线程的锁,返回值是一个局部锁
{//参数是线程要加入的锁队列的头部
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;//获取当前线程的本地锁状态数据
  //通过这个指针，线程可以访问自己的锁数组和头部指针数组。
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)//线程检查自己持有的锁，找到一个空闲位置来存储当前锁的头部指针
    {
      if (local->head[i] == NULL)
            {
              local->head[i] = head; //将传入的锁队列头指针 head 存储在当前线程的 head 数组的空闲位置 i。这表示该线程已经参与到锁队列中了。

              return &local->lock[i];//返回当前线程的 lock[i]，这是线程自己在锁队列中的节点，用于表示该线程的等待状态。
            }
    }
  return NULL;//如果遍历了整个 local->head 数组都没有找到空闲的位置，则返回 NULL，表示当前线程已经无法再嵌套获取新的锁。
}

inline glk_mcs_lock_t*
glk_mcs_get_local_unlock(glk_mcs_lock_t* head)
{
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)
    {
      if (local->head[i] == head)//找到对应锁
          {
            local->head[i] = NULL;
            return &local->lock[i];//将当前锁的位置设置为 NULL，表示当前线程不再参与这个锁队列。此操作相当于从线程的锁头指针数组中移除了该锁，意味着线程释放了这个锁。
          }
    }
  return NULL;
}


inline glk_mcs_lock_t*
glk_mcs_get_local_nochange(glk_mcs_lock_t* head)
// /查找与给定锁队列头指针 head 相对应的局部锁节点，但不改变线程本地的锁状态。
{
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)
    {
      if (local->head[i] == head)
        {
          return &local->lock[i];
          //找到与 head 匹配的锁后，返回 local->lock[i]，它是该线程在锁队列中的锁节点。这个锁节点可以表示当前线程的锁状态，用于进一步的锁操作（如查询队列中后续节点的状态）。
        }
    }
  return NULL;
}

static inline int
gls_adaptinve_mcs_lock_queue_length(glk_mcs_lock_t* lock)//计算 MCS 锁队列的长度
{ 
  int res = 1;//至少有一个节点，即当前节点
  glk_mcs_lock_t *curr = glk_mcs_get_local_nochange(lock)->next;
  //调用这个函数，获取当前线程的 MCS 锁节点。这个函数通过 lock 查找当前线程在锁队列中的局部锁节点。
  while (curr != NULL)
    {
      curr = curr->next;
      res++;
    }
  return res;//当前线程后面有多少个线程等待获取锁
}

static int
glk_mcs_lock_lock(glk_t* gl) 
//每个线程在等待锁时都会在自己的节点上自旋，而不是像其他自旋锁那样在全局锁上自旋。
{
  glk_mcs_lock_t* lock = &gl->mcs_lock;
  volatile glk_mcs_lock_t* local = glk_mcs_get_local_lock(lock);//为当前线程获取一个局部锁节点（MCS 锁的每个线程都有自己独立的锁节点）。
  local->next = NULL;//将当前线程的锁节点 local 的 next 指针设为 NULL，表示当前节点还没有后继节点。
  
  glk_mcs_lock_t* pred = swap_ptr((void*) &lock->next, (void*) local);//全局锁队列中的尾节点指针 lock->next 替换为当前线程的锁节点 local，并返回之前的尾节点（pred）。
#if GLK_DO_ADAP == 1
  const int num_acq = __sync_add_and_fetch(&gl->num_acquired, 1);
  //这是一个条件编译选项，用于控制锁的适应性（adaptive behavior）。如果开启，则 num_acq 会记录已经获取锁的次数，否则它始终为 0。
#else
  const int num_acq = 0;
#endif
  /* gl->num_acquired++; */

  if (pred == NULL)  		/* lock was free */
    {
      return num_acq;
    }//如果前一个锁节点为空，表示锁是空闲的，当前线程可以直接获取锁。
  local->waiting = 1; // word on which to spin
  //设置当前节点的 waiting 字段为 1，表示当前节点正在等待。
  pred->next = local; // make pred point to me 
  //设置当前节点的 waiting 字段为 1，表示当前节点正在等待。
  size_t n_spins = 0;
  while (local->waiting != 0) //当前线程会在自己的节点上自旋，直到 local->waiting 被前驱线程设置为 0，表示可以获取锁。
    {
      if (unlikely((n_spins++ == 1024) && gls_is_multiprogramming()))
      //如果自旋超过 1024 次，且系统处于多任务状态（gls_is_multiprogramming() 返回 true），则调用 pthread_yield() 将当前线程的 CPU 时间片让出，防止过多的自旋消耗 CPU。
	{
	  n_spins = 0;
	  pthread_yield();
	}
      PAUSE_IN();
    }
  return num_acq;//自旋结束后，表示当前线程已经成功获取锁，返回 num_acq。
}

inline int
glk_mcs_lock_trylock(glk_mcs_lock_t* lock) // MCS 锁的非阻塞尝试加锁操作,glk_mcs_lock_t* lock表示当前线程要尝试加锁的锁结构。
{ //trylock 会立即返回而不会阻塞线程。如果锁是空闲的，trylock 成功并获取锁；如果锁已经被其他线程持有，则 trylock 直接返回失败。
  if (lock->next != NULL)
    {
      return 1;
    }//检查锁是否已经被其他线程持有

  volatile glk_mcs_lock_t* local = glk_mcs_get_local_lock(lock);
  //这个函数返回当前线程的局部锁节点，表示线程在锁队列中的位置。
  local->next = NULL;//将局部锁节点的 next 字段初始化为 NULL，表示当前节点还没有后继节点。
  if (__sync_val_compare_and_swap(&lock->next, NULL, local) != NULL)
    {
      glk_mcs_get_local_unlock(lock);//在加锁失败的情况下，调用 glk_mcs_get_local_unlock 函数，将当前线程的局部锁节点释放或重置，以便后续操作可以重新使用。
      return 1;
    }
  return 0;
}

inline int
glk_mcs_lock_unlock(glk_mcs_lock_t* lock) //某个线程调度的
{
  volatile glk_mcs_lock_t* local = glk_mcs_get_local_unlock(lock);//获取当前线程对应的局部 MCS 锁节点
  volatile glk_mcs_lock_t* succ;

  if (!(succ = local->next)) /* I seem to have no succ. */
  //如果 next 为 NULL，说明当前线程没有后继线程等待锁。
    { 
      /* try to fix global pointer */
      if (__sync_val_compare_and_swap(&lock->next, local, NULL) == local) 
      //这是一个原子操作，用于将全局锁的 next 指针从当前线程的节点 local 重置为 NULL，表示锁空闲。如果其他线程没有在这个时间段内抢占锁，那么这个操作会成功。
	{
	  return 0;
	}
      do 
	{
	  succ = local->next;
	  PAUSE_IN();
	} 
      while (!succ); // wait for successor
    }
  succ->waiting = 0;//当找到后继线程 succ 时，将它的 waiting 字段设置为 0，通知它可以停止等待并获取锁。
  return 0;

}

/* **************************************** */
/* ticket */
/* **************************************** */
//锁的机制类似于排队，先来先服务。该实现通过自旋等待让线程等待自己的票号被服务。
static inline int
glk_ticket_lock_lock(glk_t* gl)//加锁
{
  glk_ticket_lock_t* lock = &gl->ticket_lock;//从传入的 glk_t 结构体中获取 ticket_lock 成员，它是具体的 Ticket Lock 对象
  const uint32_t ticket = __sync_add_and_fetch(&(lock->tail), 1);//原子操作，用于安全地递增 tail。tail 表示锁队列的尾部，每次有线程请求锁时，tail 都会递增，这样每个线程都能获得唯一的票号。
  const int distance = ticket - lock->head;
  //head 表示当前被服务的票号。如果 ticket 等于 head，说明当前线程正好是下一个被服务的，因此可以立即获得锁。
  if (likely(distance == 0))
    {
      return ticket;//直接返回票号，表示加锁成功。
    }

  if (GLK_MUST_UPDATE_QUEUE_LENGTH(ticket))//按位与操作检查 ticket 是否满足 GLK_SAMPLE_LOCK_EVERY 的条件
    {
      __sync_add_and_fetch(&gl->queue_total, distance);//distance 的值会被加到 queue_total 上
    }
  //特别是在多线程锁竞争的场景中，可以有效地降低系统开销。
  size_t n_spins = 0;
  //如果线程没有获得锁（distance != 0），它会进入一个自旋循环（busy-waiting），直到它的 ticket 对应的 head 被更新到它的票号。
  do
    {
      const int distance = ticket - lock->head;
      if (unlikely(distance == 0))
	{
	  break;
	}

      if (unlikely((n_spins++ == 1024) && gls_is_multiprogramming()))//可以去掉
	{//自旋计数器，用于跟踪线程的自旋等待次数。如果线程自旋超过 1024 次，并且系统处于多程序状态（gls_is_multiprogramming() 返回 true），它会让出 CPU (pthread_yield()) 给其他线程，并重置自旋计数器。
	  n_spins = 0;
	  pthread_yield();//让出 CPU (pthread_yield()) 给其他线程
	}
      PAUSE_IN();//减少 CPU 消耗
    }
  while (1);
  return ticket;//返回值是票号（ticket），用于表示当前线程在队列中的位置。
}

static inline int
glk_ticket_lock_unlock(glk_ticket_lock_t* lock) //解锁
{
  asm volatile("" ::: "memory");//内存屏障
#if defined(MEMCACHED)
  if (__builtin_expect((lock->tail >= lock->head), 1)) 
    {
#endif
      lock->head++;//解锁操作，head加一
#if defined(MEMCACHED)
    }
#endif
  return 0;
}

static inline int
glk_ticket_lock_trylock(glk_ticket_lock_t* lock) //尝试加锁操作
{
  uint32_t to = lock->tail;//队列的尾部
  if (lock->head - to == 1)//如果 head - tail == 1，表示锁处于空闲状态，锁没有被持有，也没有线程在排队等待获取锁。
    {
      return (__sync_val_compare_and_swap(&lock->tail, to, to + 1) != to);
      //如果 tail 的当前值等于 to，则将 tail 更新为 to + 1，表示成功获取锁；如果 tail 的当前值不等于 to，说明在此期间有其他线程修改了 tail，加锁失败。
    }
  return 1;
}

static inline int
glk_ticket_lock_init(glk_ticket_lock_t* the_lock, const pthread_mutexattr_t* a) //锁的初始化
{
  the_lock->head=1;
  the_lock->tail=0;
  return 0;
}


int
glk_is_free(glk_t* lock)//用于判断锁是否处于空闲状态（即没有线程持有锁或正在排队)
{
  do
    {
      const glk_type_t current_lock_type = lock->lock_type;
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  if (lock->ticket_lock.head - lock->ticket_lock.tail == 1)
	    {
	      return 1;
	    }
	  break;
	case MCS_LOCK:
	  if (lock->mcs_lock.next == NULL)
	    {
	      return 1;
	    }
	  break;
	case MUTEX_LOCK:
	  if (lock->mutex_lock.l.b.locked == 0)
	    {
	      return 1;
	    }
	  break;
	}

      if (likely(lock->lock_type == current_lock_type))
	{
	  break;
	}
    }
  while (1);
  return 0;
}
/* **************************************** */
/* mutex */
/* **************************************** */

static inline int
sys_futex_glk_mutex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}
//这是一个用于调用 futex 系统调用的函数。futex（Fast Userspace Mutex）是 Linux 提供的一种用户态锁定机制，允许用户进程在用户空间进行同步。futex 既可以进行忙等（用户态自旋），也可以在需要时阻塞（进入内核等待）。
//Swap uint32_t
static inline uint32_t
glk_mutex_swap_uint32(volatile uint32_t* target,  uint32_t x)
{
  asm volatile("xchgl %0,%1"
	       :"=r" ((uint32_t) x)
	       :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
	       :"memory");

  return x;
}

//Swap uint8_t
static inline uint8_t
glk_mutex_swap_uint8(volatile uint8_t* target,  uint8_t x)
{
  asm volatile("xchgb %0,%1"
	       :"=r" ((uint8_t) x)
	       :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
	       :"memory");

  return x;
}
//这两个函数分别实现了对 uint32_t 和 uint8_t 类型的原子交换操作，底层使用了 xchg 汇编指令。
#define cmpxchg(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    glk_mutex_swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     glk_mutex_swap_uint8((uint8_t*) a, b)

static inline int
glk_mutex_lock(glk_mutex_lock_t* m)//实现互斥锁的加锁操作，主要分为三步
{
  if (!xchg_8(&m->l.b.locked, 1))
    {
      return 0;
    }
//首先使用 xchg_8 尝试将 locked 字段设置为 1，如果成功则表示当前线程获取了锁，直接返回。
  const unsigned int time_spin = GLK_MUTEX_SPIN_TRIES_LOCK;
  GLK_MUTEX_FOR_N_CYCLES(time_spin,
			     if (!xchg_8(&m->l.b.locked, 1))
			       {
				 return 0;
			       }
			     PAUSE_IN();
			     );

//如果初次加锁失败（锁已被占用），线程会进入一个自旋循环，在短时间内反复尝试获取锁。
  /* Have to sleep */
  while (xchg_32(&m->l.u, 257) & 1)//尝试将锁的状态设置为 257，表示当前线程进入了等待状态。
  //如果锁仍然被占用（m->l.u & 1 为 1），线程会调用 futex 系统调用进入等待。
    {
      sys_futex_glk_mutex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);//用于让线程进入阻塞状态，等待锁的状态发生变化。
    }
  return 0;

}

static inline int
glk_mutex_unlock(glk_mutex_lock_t* m)
{
  /* Locked and not contended */
  if ((m->l.u == 1) && (cmpxchg(&m->l.u, 1, 0) == 1))
    {
      return 0;
    }
  // /首先检查 u 是否等于 1，如果是并且可以使用 cmpxchg 成功将其置为 0，表示当前锁已被成功释放，直接返回。

  /* Unlock */
  m->l.b.locked = 0;
  asm volatile ("mfence");
  //将 locked 字段设置为 0，并使用内存屏障 mfence 确保所有先前的内存操作完成后再执行接下来的代码。
  if (m->l.b.locked)
    {
      return 0;
    }
  //如果 locked 仍然为 1，说明有其他线程在尝试获取锁或竞争锁，因此直接返回 0，表示锁已经被解锁，程序可以继续执行，无需唤醒其他线程。
  /* We need to wake someone up */
  m->l.b.contended = 0;

  sys_futex_glk_mutex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);//用于唤醒等待在某个 futex 上的线程
  return 0;
}

static inline int
glk_mutex_lock_trylock(glk_mutex_lock_t* m)
{
  unsigned c = xchg_8(&m->l.b.locked, 1);
  if (!c)
    {
      return 0;
    }
  //使用 xchg_8 检查并设置 locked 字段。如果 locked 为 0，表示锁是空闲的，成功加锁并返回 0。
  return EBUSY;//如果锁已经被其他线程持有，返回 EBUSY 表示锁忙
}

static inline int
glk_mutex_init(glk_mutex_lock_t* m)
{
  m->l.u = 0;
  return 0;
}
//初始化互斥锁，将锁的状态设置为未锁定（u = 0）。这在锁创建时被调用，以确保锁的初始状态为未占用。

/* **************************************** */
/* conditionals */
/* **************************************** */


inline int
glk_cond_wait(glk_cond_t* c, glk_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;

      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }

  glk_unlock(m);

  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);

  glk_lock(m);

  return 0;
}


inline int
glk_cond_timedwait(glk_cond_t* c, glk_t* m, const struct timespec* ts)
{
  int ret = 0;
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;

      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }

  glk_unlock(m);

  struct timespec rt;
  /* Get the current time.  So far we support only one clock.  */
  struct timeval tv;
  (void) gettimeofday (&tv, NULL);

  /* Convert the absolute timeout value to a relative timeout.  */
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;

  if (rt.tv_nsec < 0)
    {
      rt.tv_nsec += 1000000000;
      --rt.tv_sec;
    }
  /* Did we already time out?  */
  if (__builtin_expect (rt.tv_sec < 0, 0))
    {
      ret = ETIMEDOUT;
      goto timeout;
    }

  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, &rt, NULL, 0);

  (void) gettimeofday (&tv, NULL);
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;
  if (rt.tv_nsec < 0)
    {
      rt.tv_nsec += 1000000000;
      --rt.tv_sec;
    }

  if (rt.tv_sec < 0)
    {
      ret = ETIMEDOUT;
    }

 timeout:
 glk_lock(m);

  return ret;
}

inline int
glk_cond_init(glk_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;

  c->l = NULL;

  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;

  return 0;
}

inline int
glk_cond_destroy(glk_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

inline int
glk_cond_signal(glk_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);

  /* Wake up a thread */
  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

  return 0;
}

inline int
glk_cond_broadcast(glk_cond_t* c)
{
  glk_t* m = c->l;

  /* No mutex means that there are no waiters */
  if (!m) return 0;

  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);

  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex_glk_mutex((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);

  return 0;
}
