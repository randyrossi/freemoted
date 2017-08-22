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

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "lockutil.h"

pxlock_t lock_create() {

    pxlock_t lock = (pxlock_t) malloc(sizeof(pxlock));

#ifdef HAVE_VSTUDIO
	lock->lock = CreateMutex(NULL,FALSE,NULL);
#else
    if (pthread_mutex_init(&lock->lock,NULL) != 0) {
       fprintf(stderr,"[lock] mutex init error\n");
       return NULL;
    }
#endif

    return lock;
}

void lock_destroy(pxlock_t lock) {
#ifdef HAVE_VSTUDIO
	CloseHandle(lock->lock);
#else
    pthread_mutex_destroy(&lock->lock);
#endif
    free(lock);
}

void lock_acquire(pxlock_t lock) {
#ifdef HAVE_VSTUDIO
	WaitForSingleObject(lock,INFINITE);
#else
    pthread_mutex_lock(&lock->lock);
#endif
}

void lock_release(pxlock_t lock) {
#ifdef HAVE_VSTUDIO
	ReleaseMutex(lock);
#else
    pthread_mutex_unlock(&lock->lock);
#endif
}

