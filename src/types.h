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

#ifndef BTREMOTE_TYPES_H
#define BTREMOTE_TYPES_H

#include "config.h"
#include "threadutil.h"

#ifdef HAVE_LIBXTST
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#endif

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
#include <giblib/giblib.h>
#endif
#endif

#ifdef HAVE_VSTUDIO
#define SOCKET_T SOCKET
#define strcasecmp(s1,s2) \
	lstrcmpiA(s1,s2)
#define strdup _strdup
#else
#define SOCKET_T int
#define closesocket(f) \
	close(f)
#define sprintf_s snprintf
#define strtok_s strtok_r
#define strcpy_s(d,n,s) strncpy(d,s,n)
#endif

typedef struct s_btcon
{
	int btfd;
} btcon;

typedef btcon* btcon_t;

typedef struct s_socon
{
	SOCKET_T sofd;
} socon;

typedef socon* socon_t;

typedef struct s_linkedcons
{
	int conId;

	pxthread_t handler_thread;

	int lircConnected;
	socon_t solirc; // connection to lircd
	pxthread_t lirc_thread; // thread reading from input stream

	// Only one of these will be established
	int inbConnected;
	btcon_t btinb; // inbound bluetooth connection
	socon_t soinb; // inbound socket connection

	// One or more of these may be established
	socon_t mythSocketCon;	// socket connection to myth frontend
	int disableMyth;

	int displayWidth;
	int displayHeight;

#ifdef HAVE_LIBXTST
	Display *dest_display;
	int screen;
	Window root;
	Visual* vis;
	Colormap cm;
#endif

	int numChunks; // total number of chunks required to cover the desktop area
	int numChunksAcross; // how many chunks across are required to cover desktop area
#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
	Imlib_Image* lastImages;
#endif
#endif
	char snapshotFilename[64];

	struct s_linkedcons* next;

} linkedcons;

typedef linkedcons* linkedcons_t;

typedef struct s_linkedcons_list
{
	linkedcons_t ptr;
	struct s_linkedcons_list* next;
} linkedcons_list;

typedef linkedcons_list* linkedcons_list_t;

#endif
