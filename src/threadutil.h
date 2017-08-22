// This file is part of Freemote Control Proxy.
//
// Freemote Control Proxy is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Freemote Control Proxy is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Freemote Control Proxy.  If not, see
// <http://www.gnu.org/licenses/>.

#ifndef THREADUTIL_H
#define THREADUTIL_H

#ifdef HAVE_VSTUDIO
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef void (*thread_proc_t) (void *);

typedef struct s_thread
{
   thread_proc_t proc;
   void *arg;
   int run; // switch to hold up the thread until thread_run()
   int waiting;
   char name[16];

#ifdef HAVE_VSTUDIO
   CRITICAL_SECTION lock;
   HANDLE cond;
   HANDLE threadh;
   DWORD threadid;
#else
   pthread_mutex_t lock;
   pthread_cond_t cond;
   pthread_attr_t attr;
   pthread_t threadid;
#endif

} thread;

typedef thread* pxthread_t;

extern pxthread_t thread_create(thread_proc_t proc, void *arg, char *name);
extern void thread_destroy(pxthread_t thread);
extern void thread_run(pxthread_t thread);
extern void thread_join(pxthread_t thread);
extern void thread_detach(pxthread_t thread);
extern void thread_wait(pxthread_t thread);
extern void thread_notify(pxthread_t thread);

#endif
