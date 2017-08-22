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

#include "config.h"

#ifdef HAVE_VSTUDIO
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#endif

#include "threadutil.h"

#ifdef HAVE_VSTUDIO

DWORD WINAPI thread_init(PVOID *data)
{
    pxthread_t thrd = (pxthread_t) data;

    // The first time we run, we hold up this thread until signalled by run
    EnterCriticalSection(&thrd->lock);
    if (thrd->run == 0) {
		WaitForSingleObject(thrd->cond,INFINITE);
    }
    LeaveCriticalSection(&thrd->lock);

    // Actually call this thread's procedure
    thrd->proc(thrd->arg);

    // Now exit
    return 0;
}

pxthread_t thread_create(thread_proc_t proc, void *arg, char* name)
{

    pxthread_t thrd = (pxthread_t) malloc(sizeof(thread));

    sprintf_s (thrd->name,16,"%s",name);
    thrd->proc = proc;
    thrd->arg = arg;
    thrd->run = 0;
    thrd->waiting = 0;

	thrd->cond = CreateEvent( 
        NULL,               // default security attributes
        FALSE,              // manual-reset event
        FALSE,              // initial state is nonsignaled
        TEXT("CondEvent")   // object name
        ); 

	InitializeCriticalSection (&thrd->lock);

	thrd->threadh = CreateThread (NULL, 0, thread_init, (PVOID)thrd, 0, &thrd->threadid);
    
    return thrd;
}

void thread_run(pxthread_t thrd)
{
    if (thrd == NULL) {
        return;
    }

    // Wake up the specified thread
    EnterCriticalSection(&thrd->lock);
    if (thrd->run == 0) {
    	thrd->run = 1;
		SetEvent(&thrd->cond);
    }
    LeaveCriticalSection(&thrd->lock);
}

void thread_join(pxthread_t thrd)
{
   if (thrd == NULL) {
       return;
   }

   // Wait for that thread to exit...
   WaitForSingleObject(thrd->threadh,INFINITE);
}

void thread_detach(pxthread_t thrd)
{
   if (thrd == NULL) {
       return;
   }

   // Wait for that thread to exit...
   WaitForSingleObject(thrd->threadh,INFINITE);
}

void thread_destroy(pxthread_t thrd)
{
   if (thrd == NULL) {
	   return;
   }
   CloseHandle(thrd->threadh);

   free(thrd);
}

void thread_wait(pxthread_t thrd)
{
   // The current thread will block and waiting will be set to 1 to indicate
   // there is a thread waiting to be notified
   EnterCriticalSection(&thrd->lock);
   thrd->waiting++;
   WaitForSingleObject(thrd->cond,INFINITE);
   LeaveCriticalSection(&thrd->lock);
}

void thread_notify(pxthread_t thrd)
{
   // Whatever thread was waiting will be woken up.  If there was no thread
   // waiting, the notification will be lost
   EnterCriticalSection(&thrd->lock);
   if (thrd->waiting > 0) {
      thrd->waiting--;
      SetEvent(&thrd->cond);
   }
   LeaveCriticalSection(&thrd->lock);
}

#else

void thread_init(void *data)
{
    pxthread_t thrd = (pxthread_t) data;

    // The first time we run, we hold up this thread until signalled by run
    pthread_mutex_lock(&thrd->lock);
    if (thrd->run == 0) {
      pthread_cond_wait(&thrd->cond, &thrd->lock);
    }
    pthread_mutex_unlock(&thrd->lock);

    // Actually call this thread's procedure
    thrd->proc(thrd->arg);

    // Now exit
    pthread_exit(NULL);
}

pxthread_t thread_create(thread_proc_t proc, void *arg, char* name)
{

    pxthread_t thrd = (pxthread_t) malloc(sizeof(thread));

    sprintf (thrd->name,"%s",name);
    thrd->proc = proc;
    thrd->arg = arg;
    thrd->run = 0;
    thrd->waiting = 0;

    if (pthread_cond_init(&thrd->cond,NULL) != 0) {
       fprintf(stderr,"[thread] cond init error\n");
       return NULL;
    }

    if (pthread_mutex_init(&thrd->lock,NULL) != 0) {
       fprintf(stderr,"[thread] mutex init error\n");
       return NULL;
    }

    if (pthread_attr_init(&thrd->attr) != 0) {
       fprintf(stderr,"[thread] attr init error\n");
       return NULL;
    }

    pthread_attr_setdetachstate(&thrd->attr,PTHREAD_CREATE_JOINABLE);

    if (pthread_create(&thrd->threadid, &thrd->attr, (void *(*)(void *))thread_init, thrd) !=0) {
       fprintf(stderr,"[thread] pthread_create error\n");
       return NULL;
    }

    return thrd;
}

void thread_run(pxthread_t thrd)
{
    if (thrd == NULL) {
        return;
    }

    // Wake up the specified thread
    pthread_mutex_lock(&thrd->lock);
    if (thrd->run == 0) {
    	thrd->run = 1;
    	pthread_cond_signal(&thrd->cond);
    }
    pthread_mutex_unlock(&thrd->lock);
}

void thread_join(pxthread_t thrd)
{
   if (thrd == NULL) {
       return;
   }

   // Wait for that thread to exit...
   int rc = pthread_join(thrd->threadid,NULL);
   if (rc != 0) {
	   printf ("error: join error\n");
   }
}

void thread_detach(pxthread_t thrd)
{
   if (thrd == NULL) {
       return;
   }

   // Wait for that thread to exit...
   int rc = pthread_detach(thrd->threadid);
   if (rc != 0) {
	   printf ("error: detach error\n");
   }
}

void thread_destroy(pxthread_t thrd)
{
   if (thrd == NULL) {
	   return;
   }

   pthread_attr_destroy(&thrd->attr);
   pthread_mutex_destroy(&thrd->lock);
   pthread_cond_destroy(&thrd->cond);
   free(thrd);
}

void thread_wait(pxthread_t thrd)
{
   // The current thread will block and waiting will be set to 1 to indicate
   // there is a thread waiting to be notified
   pthread_mutex_lock(&thrd->lock);
   thrd->waiting++;
   pthread_cond_wait(&thrd->cond, &thrd->lock);
   pthread_mutex_unlock(&thrd->lock);
}

void thread_notify(pxthread_t thrd)
{
   // Whatever thread was waiting will be woken up.  If there was no thread
   // waiting, the notification will be lost
   pthread_mutex_lock(&thrd->lock);
   if (thrd->waiting > 0) {
      thrd->waiting--;
      pthread_cond_signal(&thrd->cond);
   }
   pthread_mutex_unlock(&thrd->lock);
}


#endif
