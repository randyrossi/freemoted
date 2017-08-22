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

#ifndef LOCKUTIL_H
#define LOCKUTIL_H

#ifdef HAVE_VSTUDIO
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct s_pxlock
{
#ifdef HAVE_VSTUDIO
    HANDLE lock;
#else
    pthread_mutex_t lock;
#endif
} pxlock;

typedef pxlock* pxlock_t;

extern pxlock_t lock_create();
extern void lock_destroy(pxlock_t lock);
extern void lock_acquire(pxlock_t lock);
extern void lock_release(pxlock_t lock);

#endif
