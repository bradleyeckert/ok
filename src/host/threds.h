#ifndef __THREDS_H__
#define __THREDS_H__

#ifdef _MSC_VER

#include <thread>
#define YieldThread Thread.Yield
#define JoinThread thrd_join

#else // pthreads is available in Code::Blocks and Linux

#include <pthread.h>
#define YieldThread sched_yield
#define JoinThread pthread_join

#endif

#endif /* __THREDS_H__ */
