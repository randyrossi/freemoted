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
//
// NOTE: Define CLIENT_COMPAT to keep this working for old iphone/bb/playbook
// clients that expect to read '# ' responses after every command.  By
// default the server will be silent after processing commands and those
// clients will hang.

#include "config.h"
#include "main.h"

#ifdef HAVE_VSTUDIO
#include <initguid.h>
#include <winsock2.h>
#include <ws2bth.h>
#include <BluetoothAPIs.h>
#endif

#include "types.h"
#include "threadutil.h"
#include "lockutil.h"
#include "screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HAVE_VSTUDIO
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <arpa/inet.h>
#endif

#ifdef HAVE_MACOSX
#include <objc/runtime.h>
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

#ifdef HAVE_WIN32
#include <windows.h>
#include <winuser.h>
#endif

#ifdef HAVE_LIBBLUETOOTH
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#ifndef HAVE_VSTUDIO
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <termios.h>
#endif

#ifdef HAVE_LIBXTST
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/XF86keysym.h>
#endif

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
#include <giblib/giblib.h>
#endif
#endif

#include "regmdns.h"

// Outbound control types
#define MYTHFE 1
#define LIRC 2
#define MYTHDB 4
#define DESKTOP 8
#define RESERVED 16
#define SHELL 32

#define UP 1
#define DOWN 2
#define DOWN_THEN_UP 3

#define _QUOTED_SYSCONFDIR(x) #x
#define QUOTED_SYSCONFDIR(x) _QUOTED_SYSCONFDIR(x)

#define MAX_CONFIG_LINES 512
#define MAX_CONFIG_LINE_LEN 256
#define MAX_FILENAME_LEN 64
#define MAX_DIRECTIVES_PER_COMMAND 3
#define MAX_CONFIG_DIRECTIVE_LEN 80
#define MAX_KEYCODES 256
#define MAX_KEYCODE_LEN 16
#define MAX_SEQUENCES 10
#define MAX_KEYCODES_PER_SEQUENCE 3
#define MAX_PROTOCOL_COMMAND_LEN 64
#define MAX_IMAGE_READ_BUFFER 16384
// Allow up to this many delta regions for live view
// before reverting to full update
#define MAX_DELTA_REGIONS 32
// Allow up to this percentage of changed area overall
// before reverting to full update
#define MAX_DELTA_RATIO 0.60

// Full screen shot, param1 is quality, param2 is ignored
#define SCREENSHOT_MODE_FULL 0
// Try to figure delta since last screen shot, param1 is quality, param2 is ignored
#define SCREENSHOT_MODE_DELTA 1
// Fill in details around cursor location, param1 is quality, param2 is radius
#define SCREENSHOT_MODE_CURSOR 2

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define ABS(a)	 (((a) < 0) ? -(a) : (a))

pxlock_t lock;

int controlMethod = DESKTOP;
int proxyType = CT_SOCKET;
int quiet = 1;
int terminating = 0;

// For mythtv frontend connection
char *mythHost = (char*) "localhost";
int mythPort = 6546;

// For mythtv backend database connection
char *mysqlHost = (char*) "localhost";
int mysqlPort = 3306;
char *mysqlUser = (char*) "mythtv";
char *mysqlPassword = (char*) "mythtv";

// For bluetooth proxy (must match channel given to sdptool on linux)
// On windows, this gets dynamically chosen when the process starts
int btChannel = 1;
int btRegisteredThisProcess = 0;

// For socket proxy
int acceptPort = 8001;

#ifdef HAVE_VSTUDIO
LPWSTR configFile = NULL;
#else
char* configFile = NULL;
#endif

// For fake lircd
char *lircHost = (char*) "localhost";
int lircPort = 8888;

// For XWindows connection
char *xDisplay = (char*) ":0.0";

// Populated from freemoted.conf file
int numKeyCmds = 0;
int numTranslatedSymbols = 0;
char pxyCmd[MAX_CONFIG_LINES][MAX_CONFIG_DIRECTIVE_LEN]; // proxy command
char lircCmd[MAX_CONFIG_LINES][MAX_CONFIG_DIRECTIVE_LEN]; // lircd code
char keyCmd[MAX_CONFIG_LINES][MAX_CONFIG_DIRECTIVE_LEN]; // x keystroke
char mythCmd[MAX_CONFIG_LINES][MAX_CONFIG_DIRECTIVE_LEN]; // myth command
char shellCmd[MAX_CONFIG_LINES][MAX_CONFIG_DIRECTIVE_LEN]; // shell command
int cmdOrder[MAX_CONFIG_LINES][5];

// index%2 even = one character symbol
// index%2 odd  = key text it will expand to
char translatedSymbols[MAX_CONFIG_LINES][MAX_CONFIG_LINE_LEN];

int conSequence = 0;
SOCKET_T inbs;	// inbound socket

int bound;	// were we already bound to a port yet?

linkedcons_t allCons = NULL;

int numKeySyms=0;
char keyCodes[MAX_KEYCODES][MAX_KEYCODE_LEN];
int keySyms[MAX_KEYCODES];
int b[MAX_KEYCODES];

int totalBytesSentSession = 0;
int totalRegionsSentSession = 0;

long prevClick = 0;
long clickCount = 1;
int eventNumber = 16383;

freemoted_state_proc stateCallback = NULL;

void connectToMyth(linkedcons_t linked);

int writeAll(SOCKET_T fd,char* buf,int len)
{
    int pos = 0;
    int remain = len;
    int todo;
    int n;
    while (remain > 0) {
        if (remain > 1300) {
		todo = 1300;
	} else {
		todo = remain;
	}
	n = send(fd,buf+pos,todo,0);
	if (n <= 0) {
		return -1;
	}
	pos += n;
	remain -= n;
   }
   return len;
}

void setState(int state,void *arg)
{
	if (stateCallback != NULL) {
		stateCallback(state,arg);
	}
}

void cleanup(linkedcons_t linked) {
	linkedcons_t cur;
	linkedcons_t prev;
	if (!linked) { return; }

	if (!quiet) {
		printf ("cleanup %d\n",linked->conId);
	}

	if (linked->lircConnected) {
		linked->lircConnected = 0;

		// Join up with the lirc consumer thread
		if (linked->lirc_thread != NULL) {
			thread_join(linked->lirc_thread);
			thread_destroy(linked->lirc_thread);
			linked->lirc_thread = NULL;
		}

		// Shutdown the lirc connection
		if (linked->solirc != NULL) {
			closesocket(linked->solirc->sofd);
			free(linked->solirc);
			linked->solirc = NULL;
		}
	}

	if (linked->btinb != NULL) {
		closesocket(linked->btinb->btfd);
		free(linked->btinb);
		linked->btinb = NULL;
	}

	if (linked->soinb != NULL) {
		closesocket(linked->soinb->sofd);
		free(linked->soinb);
		linked->soinb = NULL;
	}

	if (linked->mythSocketCon != NULL) {
		closesocket(linked->mythSocketCon->sofd);
		free(linked->mythSocketCon);
		linked->mythSocketCon = NULL;
	}

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
	if (linked->lastImages != NULL) {
		lock_acquire(lock);
		int c;
		for (c=0;c<linked->numChunks;c++) {
			if (linked->lastImages[c] != NULL) {
				gib_imlib_free_image(linked->lastImages[c]);
			}
		}
		free(linked->lastImages);
		linked->lastImages = NULL;
		lock_release(lock);
	}
#endif
#endif

#ifdef HAVE_LIBXTST

	if (linked->dest_display != NULL) {
		XCloseDisplay(linked->dest_display);
		linked->dest_display;
	}

#endif

	// Remove myself from the list of connections...
	cur = allCons;
	prev = NULL;
	while (cur) {
		if (cur == linked) {
			if (prev != NULL) {
				prev->next = cur->next;
				break;
			} else {
				allCons = cur->next;
				break;
			}
		}
		cur = cur->next;
	}

	// We self terminated, so detach since no one will join with us
	if (linked->inbConnected == 1) {
		thread_detach(linked->handler_thread);
		thread_destroy(linked->handler_thread);
		free(linked);
	}

}

int f_count_connections()
{
	linkedcons_t cur;
	int c = 0;

	cur = allCons;
	while (cur != NULL) {
		c++;
		cur = cur->next;
	}
	
	return c;
}


void f_shutdown()
{

	linkedcons_t cur;
	linkedcons_list_t prev;
	linkedcons_list_t head;
	linkedcons_list_t new_con;
	linkedcons_list_t cur2;
	linkedcons_list_t tmp;

	if (terminating == 1) {
		return;
	}
	terminating = 1;

	setState(FREEMOTED_STATUS_STOPPING,NULL);

	// Make a copy of the connections list
	prev = NULL;
	head = NULL;
	cur = allCons;
	while (cur != NULL) {
		new_con = (linkedcons_list_t) malloc(sizeof(linkedcons_list));
		if (prev != NULL) {
			prev->next = new_con;
		}
		if (head == NULL) {
			head = new_con;
		}
		new_con->ptr = cur;
		new_con->next = NULL;
		prev = new_con;
		cur = cur->next;
	}

	cur2 = head;
	// Remove and free all connections to exit
	while (cur2 != NULL) {
		// Signal to this connection it should terminate and cleanup
		cur2->ptr->inbConnected = 0;
		cur2=cur2->next;
	}

	// Now wait for everyone to terminate, free our copy as we go
	cur2 = head;
	while (cur2 != NULL) {
		// Wait for the thread to end
		thread_join(cur2->ptr->handler_thread);
		thread_destroy(cur2->ptr->handler_thread);
		free(cur2->ptr);

		tmp = cur2;
		cur2=cur2->next;
		free(tmp);
	}

	if (inbs != 0) {
		closesocket(inbs);
		inbs = 0;
		// main thread will now exit
	}
	
}

#ifndef HAVE_VSTUDIO
void sigterm(int sig)
{
	if (sig == SIGPIPE) {
		printf ("ignoring SIGPIPE\n");
		return;
	}

	if (!quiet) {
        	printf ("Shutdown %d\n",sig);
	}

	f_shutdown();
}
#endif


int startsWith(const char *s1, const char *s2) {
	int l1 = strlen(s1);
	int l2 = strlen(s2);
	int i;
	if (l2 > l1) { return 0; }

	for (i=0;i<l2;i++) {
		if (s1[i] != s2[i]) {
			return 0;
		}
	}
	return 1;
}

char* trim(char* str) {
	int i;
	char* dup = strdup(str);

	int len = strlen(dup);
	for (i = len - 1; i >= 0; i--) {
		if (dup[i] == '\n' || dup[i] == '\r') {
			dup[i] = '\0';
		} else {
			return dup;
		}
	}
	return dup;
}

int readLine(char* buf, SOCKET_T fd) {
	int pos = 0;
	int prevCh = -1;
	while (1 == 1) {
		int n = recv(fd, buf + pos, 1,0);
		if (n <= 0) {
			return n;
		} else {
			if (buf[pos] == '\n') {
				buf[pos + 1] = '\0';
				return pos+1;
			}
			else if(buf[pos] == ' ' && prevCh == '#') {
				buf[pos + 1] = '\0';
				return pos+1;
			}
			prevCh = buf[pos];
			pos++;
		}
	}
}

int sendImageResponseHeader(SOCKET_T writefd, int numFrames)
{
	char numStr[16];
	sprintf_s(numStr,16, "%d\n", numFrames);
	return writeAll(writefd, numStr, strlen(numStr));
}

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
int sendImage(const char* filename, SOCKET_T writefd, int top, int left, int w, int h, int chunkIndex) {
	struct stat shotstat;
	int status;

	int fd = open(filename, O_RDWR);

	if (fd > 0) {
		status = fstat(fd, &shotstat);
		int numLeft = shotstat.st_size;
		char numStr[MAX_PROTOCOL_COMMAND_LEN];
		sprintf(numStr, "%d %d %d %d %d %d\n", numLeft, top, left, w, h, chunkIndex);
		if (writeAll(writefd, numStr, strlen(numStr)) <0) {
			close(fd);
			return -1;
		}

		char buf[MAX_IMAGE_READ_BUFFER];
		int num;
		while (numLeft > 0) {
			num = read(fd, buf, MAX_IMAGE_READ_BUFFER);
			if (num <= 0) {
				close(fd);
				return -1;
			}

			if (writeAll(writefd, buf, num) < 0) {
				close(fd);
				return -1;
			}

			numLeft -= num;
		}

		close(fd);
		return shotstat.st_size;
	}
	return -1;
}
#endif
#endif

int snapshot(linkedcons_t linked, SOCKET_T writefd, int mode, int param1, int param2) {

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
    int numRegions;
    int top[MAX_DELTA_REGIONS];
    int left[MAX_DELTA_REGIONS];
    int imw[MAX_DELTA_REGIONS];
    int imh[MAX_DELTA_REGIONS];
    Imlib_Image im[MAX_DELTA_REGIONS];
    char numStr[MAX_PROTOCOL_COMMAND_LEN];

    if (linked->lastImages == NULL) {
    	sprintf(numStr, "0\n");
    	if (writeAll(writefd, numStr, strlen(numStr)) < 0) {
	    return -1;
	}
    	return 0;
    }

    sprintf(numStr, "%d\n", linked->numChunks);
    if (writeAll(writefd, numStr, strlen(numStr)) < 0) {
    	return -1;
    }

    imlib_context_set_display(linked->dest_display);
    imlib_context_set_visual(linked->vis);
    imlib_context_set_colormap(linked->cm);
    imlib_context_set_color_modifier(NULL);
    imlib_context_set_operation(IMLIB_OP_COPY);

    int chunkIndex;
    int totalBytesSent = 0;
    int totalRegionsSent = 0;
    int totalFullRegionsSent = 0;
    // Since the desktop area may be larger than the biggest image we
    // are allowed to send, we need to chunk it up according that that
    // maximum.  For example, if our desktop area is 1280 x 800 and our
    // maximum chunk size is 800 x 600, then we need at least 4 chunks
    // in a 2 x 2 grid.  This is figured out dynamically when we connect
    // to the desktop display.
    int sndErr = 0;
    for (chunkIndex = 0; chunkIndex < linked->numChunks && sndErr == 0; chunkIndex++) {
    	// Every time we analyse a chunk, we must reset the numRegions counter
    	numRegions = 0;

    	// Compute the actual coordinates on the desktop where this
	// chunk lives.
    	int chunkTop = chunkIndex / linked->numChunksAcross * MAX_SCREENSHOT_CHUNK_HEIGHT;
    	int chunkLeft = chunkIndex % linked->numChunksAcross * MAX_SCREENSHOT_CHUNK_WIDTH;
    	int chunkWidth = MAX_SCREENSHOT_CHUNK_WIDTH;
    	int chunkHeight = MAX_SCREENSHOT_CHUNK_HEIGHT;

    	// Be careful not to extend past the actual desktop limits.
    	if (chunkTop + chunkHeight >= linked->displayHeight) {
    		chunkHeight = linked->displayHeight - chunkTop;
    	}
    	if (chunkLeft + chunkWidth >= linked->displayWidth) {
    	    chunkWidth = linked->displayWidth - chunkLeft;
    	}

    	// Full Screen shot means one region (for this chunk) but if we
    	// were asked a delta but have nothing previously to compare, we
    	// have to turn this into a full request
		if (mode == SCREENSHOT_MODE_FULL || (mode == SCREENSHOT_MODE_DELTA && linked->lastImages[chunkIndex] == NULL)) {
			// Cut out the region as defined by the full
			// chunk coordinates
			im[numRegions] = gib_imlib_create_image_from_drawable(linked->root,0,chunkLeft,chunkTop,chunkWidth,chunkHeight,1);
			if (im[numRegions] != NULL) {
				// Free previous full image and save a copy
				// of this new one
				if (linked->lastImages[chunkIndex] != NULL) {
					gib_imlib_free_image(linked->lastImages[chunkIndex]);
				}
				linked->lastImages[chunkIndex] = gib_imlib_clone_image(im[numRegions]);

				// Place the region info in the array.
				// This will be our only region.
				imw[0] = chunkWidth;
				imh[0] = chunkHeight;

				// top is relative to this chunk's top
				// computed from chunkIndex
				top[0] = 0;

				// left relative to this chunk's left
				// computed from chunkIndex
				left[0] = 0;
				numRegions=1;
				totalFullRegionsSent++;
			}
		} else if (mode == SCREENSHOT_MODE_DELTA) {
			// Grab the full chunk but compare to the last one
			// we took (for the same chunk) and  try to only
			// send the regions within that chunk that
			// have changed.
			Imlib_Image nextImage = gib_imlib_create_image_from_drawable(linked->root,0,chunkLeft,chunkTop,chunkWidth, chunkHeight,1);
			if (nextImage != NULL) {
				float ratio = findDeltas(MAX_DELTA_REGIONS, chunkWidth, chunkHeight, linked->lastImages[chunkIndex], nextImage, &numRegions, im, imw, imh, top, left);

				if (linked->lastImages[chunkIndex] != NULL) {
					gib_imlib_free_image(linked->lastImages[chunkIndex]);
				}
				linked->lastImages[chunkIndex] = nextImage;

				// If the percentage of area that has changed
				// is too high or too many regions were
				// found, let's just send back a full chunk
				// as one region.  Probably better.
				if (ratio >= MAX_DELTA_RATIO || numRegions >= MAX_DELTA_REGIONS) {
					// Free up all the delta regions
					int re;
					for (re = 0;re<numRegions;re++) {
						gib_imlib_free_image(im[re]);
					}
					im[0] = gib_imlib_clone_image(nextImage);

					imw[0] = chunkWidth;
					imh[0] = chunkHeight;
					// top is relative to this chunk's
					// top computed from chunkIndex
					top[0] = 0;
					// left is relative to this chunk's
					// left computed from chunkIndex
					left[0] = 0;
					numRegions = 1;
					totalFullRegionsSent++;
				}
			}
		} else if (mode == SCREENSHOT_MODE_CURSOR) {
	        // Find where the mouse is and take a snap around there.
		// One region.
	        if (param2 < 2) { param2 = 2; }
	        if (param2 > 128) { param2 = 128; }

	        Window wr,cr;
	        int rx,ry;
	        int wx,wy;
	        unsigned int mask;
	        XQueryPointer(linked->dest_display,linked->root,&wr,&cr,&rx,&ry,&wx,&wy,&mask);

	        // Make sure region being considered doesn't extend past
	        // borders of whole screen
	        int l = rx - param2/2; if (l < 0) { l = 0; } else if (l+param2 >= linked->displayWidth) { l = linked->displayWidth-param2-1; }
	        int t = ry - param2/2; if (t < 0) { t = 0; } else if (t+param2 >= linked->displayHeight) { t = linked->displayHeight-param2-1; }

	        // Now make sure we only send the region belonging to
	        // this current chunk
	        int r = l+param2;
	        int b = t+param2;

	        int insideThisChunk =
	        		(l >= chunkLeft && l <= (chunkLeft+chunkWidth)) ||
	        		(r >= chunkLeft && r <= (chunkLeft+chunkWidth)) ||
	        		(t >= chunkTop && t <= (chunkTop+chunkHeight)) ||
	        		(b >= chunkTop && b <= (chunkTop+chunkHeight));

	        if (insideThisChunk) {
	        	// Union between the two areas
		        l = MAX(l, chunkLeft);
		        r = MIN(r, chunkLeft+chunkWidth);
		        t = MAX(t, chunkTop);
		        b = MIN(b, chunkTop+chunkHeight);

		        im[0] = gib_imlib_create_image_from_drawable(linked->root,0,l,t,r-l,b-t,1);
       	    	if (im[0] != NULL) {
	                imw[0] = r-l;
	                imh[0] = b-t;
	                // we need to report left relative to chunk top/left
	                left[0] = l-chunkLeft;
	                // we need to report top relative to chunk top/left
	                top[0] = t-chunkTop;
	                numRegions=1;
       	    	}
       	    }
	    }

		// Did we find anything?
		if (numRegions > 0) {
			// Tell the client how many regions we are
			// sending back for this chunk
			if (sendImageResponseHeader(writefd,numRegions) < 0) {
				return -1;
			}

			// Send back each region.  Even though the client
			// could figure out the chunk from the outer loop,
			// we spit out the chunk index explicitely with
			// each region.
			int r;
			totalRegionsSent+= numRegions;
			totalRegionsSentSession += numRegions;
			for (r = 0;r<numRegions && sndErr == 0;r++) {
				imlib_context_set_image(im[r]);

				imlib_image_attach_data_value("quality",NULL,param1,NULL);
				Imlib_Load_Error err;
				gib_imlib_save_image_with_error_return(im[r], linked->snapshotFilename, &err);
				if (err != IMLIB_LOAD_ERROR_NONE) {
					if (!quiet) {
						fprintf (stderr,"error saving screenshot image");
					}
					sprintf(numStr, "%d %d %d %d %d %d\n", 0, 0, 0, 0, 0, chunkIndex);
					if (writeAll(writefd, numStr, strlen(numStr)) < 0) {
						sndErr = 1;
						break;
					}
				} else {
					int sent = sendImage(linked->snapshotFilename, writefd,top[r],left[r],imw[r],imh[r],chunkIndex);
					if (sent < 0) {
						sndErr = 1;
						break;
					}
					totalBytesSent += sent;
					totalBytesSentSession+=sent;
				}
			}
			// Free all images in this chunk
			for (r = 0;r<numRegions;r++) {
				gib_imlib_free_image(im[r]);
			}
		} else {
			if (sendImageResponseHeader(writefd,0) < 0) {
				sndErr = 1;
			}
		}
    }
    if (sndErr != 0) {
    	return -1;
    }
#endif
#else
    // No support
    if (sendImageResponseHeader(writefd,0) < 0) {
    	return 0;
    }
#endif
	return 0;
}


// relay data coming from myth back to inbound connection
int relayMyth(linkedcons_t linked, SOCKET_T writefd) {
	char command[1024];
	while (1 == 1) {
        	if (readLine(command, linked->mythSocketCon->sofd) <= 0) {
        		fprintf (stderr,"error talking to myth:\n");
                	return 0;
        	}

        	if (!quiet) {
        		printf ("from myth: %s\n",command);
        	}

		if (writefd != -1) {
			if (writeAll(writefd,command,strlen(command)) < 0) {
				return 0;
			}
		}

		if (strcmp(command,"# ") == 0) {
			break;
		}
	}
	return 1;
}

// just consume until the first prompt and throw it away
int consumeUntilPrompt(linkedcons_t linked) {
	return relayMyth(linked,-1);
}

int writeLirc(linkedcons_t linked, char *lircCommand, SOCKET_T readfd) {
	if ((controlMethod & LIRC) != 0 && lircCommand != NULL && lircCommand[0] != '\0') {
		// Dispatch to all clients
		char *m = (char*)"SIMULATE 0000000000000000 00 %s Remote\n";
		char cmd[64];
		sprintf_s(cmd, 64, m, lircCommand);
		if (writeAll(linked->solirc->sofd, cmd, strlen(cmd)) < 0) {
			return -1;
		}
		if (!quiet) {
			printf("lirc: '%s'\n", lircCommand);
		}
		if (writeAll(readfd, "OK\n# ", 5) < 0) {
			return -1;
		}
		return 1;
	}
	return 0;
}

int strToKeySym(char* keycodeStr, int *keysym, int *breakcode) {
	int j=0;
	*keysym = -1;

#ifdef HAVE_LIBXTST
	if (strlen(keycodeStr) == 1) {
		// Can use the ascii char as the key sym
		if (keycodeStr[0] >= 32 && keycodeStr[0] <= 126) {
			*keysym = keycodeStr[0];
			return 1;
		} else if (keycodeStr[0] == 127) {
			// fall thru	
		} else {
			return 0;
		}
	}
#endif
	if (strlen(keycodeStr) == 1 && keycodeStr[0] == 127) {
	    strcpy_s(keycodeStr,4,"del");
	}

	for (j=0;j<numKeySyms;j++) {
		if (strcasecmp(keycodeStr,keyCodes[j])==0) {
			*keysym = keySyms[j];
			*breakcode = b[j];
			return 1;
		}
	}

	return 0;
}

// For Win32, we have to provide the shift sequence for anything that
// actually needs shift on the keyboard
void applySymbolTranslation(char *tmp)
{
	// Is it one character?  Maybe we have to translate it to a key
	// sequence...
	if (strlen(tmp) == 1) {
		char ch = tmp[0];
		int i;
		for (i=0;i<numTranslatedSymbols;i+=2) {
			if (ch == translatedSymbols[i][0]) {
				sprintf_s (tmp,MAX_PROTOCOL_COMMAND_LEN,"%s",translatedSymbols[i+1]);
				break;
			}
		}
	}
}

int writeKeys(linkedcons_t linked, char *command, int pressInfo, SOCKET_T readfd) {

	// Format is sym1+sym2+sym3+...|sym1+sym2+sym3+...|...
	// DOWN presses syms in order
	// UP releases syms in reverse order

	// up to MAX_SEQUENCES sequences with MAX_KEYCODES_PER_SEQUENCE keys
	// pressed per sequence
	char allSequences[MAX_SEQUENCES][MAX_KEYCODES_PER_SEQUENCE][MAX_KEYCODE_LEN];
	int numKeysInSequence[MAX_SEQUENCES];
	int numSequences;

	char *save1,*save2;
	char* arg;

	char* next;
	int sequence;
	int key;

	// Handle key commands here using XWin
	if ((controlMethod & DESKTOP) == 0) {
		return 0;
	}

	arg = strdup(command);
	next = NULL;
	sequence = 0;
	key = 0;
	do {
		next = strtok_s(arg,"|",&save1);
		arg = NULL;
		key = 0;
		if (next != NULL) {
			char* arg2 = strdup(next);
			char *next2 = NULL;
			do {
				next2 = strtok_s(arg2,"+",&save2);
				arg2 = NULL;
				if (next2 != NULL) {
					strcpy_s(allSequences[sequence][key],MAX_KEYCODE_LEN,next2);
					key++;
				}
			} while (next2!= NULL);
			free(arg2);
		}
		numKeysInSequence[sequence]=key;
		sequence++;
	} while (next != NULL);
	numSequences = sequence;
	free(arg);

	for (sequence=0;sequence<numSequences;sequence++) {
		if ((pressInfo & DOWN) !=0) {
			for (key=0;key<numKeysInSequence[sequence];key++) {
				int keysym,breakcode;
				if (strToKeySym(allSequences[sequence][key],&keysym,&breakcode)) {
					if (keysym >= 0) {
						if (!quiet) {
							printf ("X: keysym %d %d %s\n",keysym,1,allSequences[sequence][key]);
						}
						lock_acquire(lock);
#ifdef HAVE_LIBXTST

						XTestFakeKeyEvent(linked->dest_display,XKeysymToKeycode(linked->dest_display,keysym),1,CurrentTime);
						XFlush(linked->dest_display);
#endif
#ifdef HAVE_WIN32
						keybd_event(keysym,breakcode,KEYEVENTF_EXTENDEDKEY,0);
#endif
#ifdef HAVE_MACOSX
						//objc_msgSend(callback_obj, sel_getUid("keypress:"),keysym);
						CGPostKeyboardEvent((CGCharCode) 0, (CGKeyCode)keysym, true);
#endif
						lock_release(lock);
					}
				}
			}
		}

		if ((pressInfo & UP) !=0) {
			for (key=numKeysInSequence[sequence]-1;key>=0;key--) {
				int keysym,breakcode;
				if (strToKeySym(allSequences[sequence][key],&keysym,&breakcode)) {
					if (keysym >= 0) {
						if (!quiet) {
							printf ("X: keysym %d %d %s\n",keysym,0,allSequences[sequence][key]);
						}
						lock_acquire(lock);
#ifdef HAVE_LIBXTST
						XTestFakeKeyEvent(linked->dest_display,XKeysymToKeycode(linked->dest_display,keysym),0,CurrentTime);
						XFlush(linked->dest_display);
#endif
#ifdef HAVE_WIN32
						keybd_event(keysym,breakcode,KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY,0);
#endif
#ifdef HAVE_MACOSX
						//objc_msgSend(callback_obj, sel_getUid("keypress:"),keysym);
						CGPostKeyboardEvent((CGCharCode) 0, (CGKeyCode)keysym, false);
#endif
						lock_release(lock);
					}
				}
			}
		}
	}
	
#ifdef CLIENT_COMPAT
	if (writeAll(readfd, "OK\n# ", 5) < 0) {
		return -1;
	}
#endif
	return 1;
}

int writeMyth(linkedcons_t linked, char *command, SOCKET_T readfd) {
	if ((controlMethod & MYTHFE) != 0) {
		if (linked->disableMyth != 0) {
			connectToMyth(linked);
		}
		if (linked->disableMyth == 0) {
			if (writeAll(linked->mythSocketCon->sofd, command, strlen(command)) < 0) {
				return -1;
			}

			if (!quiet) {
				printf("myth: '%s'\n", command);
			}
			// Relay back to our client what mythfe said
			if (relayMyth(linked,readfd) == 0) {
				linked->disableMyth = 1;
				if (writeAll(readfd, "OK\n# ", 5) < 0) {
					return -1;
				}
			}
			return 1;
		}
	}
	return 0;
}

int processCommand(char *command, linkedcons_t linked, SOCKET_T readfd) {
	char tmp[MAX_PROTOCOL_COMMAND_LEN];
	char tmp2[MAX_PROTOCOL_COMMAND_LEN];
#ifdef HAVE_WIN32			
	POINT point;
#endif
	char* compare;
	int i;
	int z;
	if (!quiet) {
		printf ("raw: %s\n",command);
	}

	if (command[0] == '\0' || command[0] == '\n' || command[0] == '\r') {
		// nothing do to
#ifdef CLIENT_COMPAT
		if (writeAll(readfd, "OK\n# ", 5) < 0) {
			return -1;
		}
#endif
		return 1;
	}

	if (command[0] == 'q' &&
		command[1] == 'u' &&
		command[2] == 'i' &&
		command[3] == 't') {
		return -1;
	}

	if (command[0] == 's' &&
		command[1] == 't' &&
		command[2] == 'a' &&
		command[3] == 't') {
		linkedcons_t cur = allCons;
		char msg[32];
		while (cur) {
			sprintf_s(msg,16,"conId %d\n",cur->conId);
			if (writeAll(readfd,msg,strlen(msg)) < 0) {
				return -1;
			}
			cur = cur->next;
		}
		writeAll(readfd, "OK\n# ", 5);
		return 1;
	}

	// First check for mapping btn to lirc
	// If no lirc, replace command with equivalent key command
	compare = trim(command); // must be freed before we leave
	for (i = 0; i < numKeyCmds; i++) {
		if (strcmp(pxyCmd[i], compare) == 0) {
			int o;
			for (o=0;o<MAX_DIRECTIVES_PER_COMMAND;o++) {
				if (cmdOrder[i][o] == LIRC) {
					int z = writeLirc(linked,lircCmd[i],readfd);
					if (z == 1) {
						free(compare);
						return 1;
					} else if (z == -1) {
						free(compare);
						return -1;
					}
				} else if (cmdOrder[i][o] == DESKTOP) {
					int z = writeKeys(linked,keyCmd[i],DOWN_THEN_UP,readfd);
					if (z == 1) {
						free(compare);
						return 1;
					} else if (z == -1) {
						free(compare);
						return -1;
					}
				} else if (cmdOrder[i][o] == MYTHFE) {
					int z = writeMyth(linked,mythCmd[i],readfd);
					if (z == 1 ) {
						free(compare);
						return 1;
					} else if (z == -1) {
						free(compare);
						return -1;
					}
				} else if (cmdOrder[i][o] == SHELL) {
					int rc = system(shellCmd[i]);
					if (writeAll(readfd, "OK\n# ", 5) < 0) {
						free(compare);
						return -1;
					}
					free(compare);
					return 1;
				}
			}
		}
	}

	free(compare);

	// Handle mouse movement and buttons
	if (command[0] == 'm' && (command[1] == 'm' || command[1] == 'p') && command[2] == ' ') {
		if ((controlMethod & DESKTOP) != 0) {
			int x, y;
			if (command[1] == 'p') {
#ifdef HAVE_VSTUDIO
				sscanf_s(command, "mp %d,%d", &x, &y);
#else
				sscanf(command, "mp %d,%d", &x, &y);
#endif
			} else {
#ifdef HAVE_VSTUDIO
				sscanf_s(command, "mm %d,%d", &x, &y);
#else
				sscanf(command, "mm %d,%d", &x, &y);
#endif
			}
#ifdef HAVE_LIBXTST
			lock_acquire(lock);
			XTestFakeRelativeMotionEvent(linked->dest_display, x, y, 0);
			XFlush(linked->dest_display);
			lock_release(lock);

			if (command[1] == 'p') {
				// Instead of being silent, spit out our current position
				Window wr,cr;
				int rx,ry,wx,wy;
				unsigned int mask;
				lock_acquire(lock);
				XQueryPointer(linked->dest_display,linked->root,&wr,&cr,&rx,&ry,&wx,&wy,&mask);
				lock_release(lock);
				char tmp[MAX_PROTOCOL_COMMAND_LEN];
				sprintf(tmp,"%d %d\n",rx,ry);
				if (writeAll(readfd,tmp,strlen(tmp))< 0) {
					return -1;
				}
			}
#endif
#ifdef HAVE_WIN32			
			lock_acquire(lock);
			GetCursorPos(&point);
			SetCursorPos(point.x+x, point.y+y);
			lock_release(lock);

			if (command[1] == 'p') {
				lock_acquire(lock);
				GetCursorPos(&point);
				lock_release(lock);
				// Instead of being silent, spit out our current position
				sprintf_s(tmp,MAX_PROTOCOL_COMMAND_LEN,"%d %d\n",point.x,point.y);
				if (writeAll(readfd,tmp,strlen(tmp)) < 0) {
					return -1;
				}
			}
#endif
#ifdef HAVE_MACOSX
			lock_acquire(lock);
			CGEventRef ourEvent = CGEventCreate(NULL);
			CGPoint ourLoc = CGEventGetLocation(ourEvent);
			
			CGDisplayCount displayCount = 0;
			CGDirectDisplayID displayID;
			CGGetDisplaysWithPoint(CGPointMake(ourLoc.x, ourLoc.y), 1, &displayID, &displayCount);
			
			CGRect displayRect = CGDisplayBounds(displayID);

			ourLoc.x+=x; ourLoc.y+=y;
			if (ourLoc.x < displayRect.origin.x) { ourLoc.x = displayRect.origin.x; }
			else if (ourLoc.x > displayRect.origin.x + displayRect.size.width-1) { ourLoc.x = displayRect.origin.x + displayRect.size.width-1; }
			if (ourLoc.y < displayRect.origin.y) { ourLoc.y = displayRect.origin.y; }
			else if (ourLoc.y > displayRect.origin.y + displayRect.size.height-1) { ourLoc.y = displayRect.origin.y + displayRect.size.height-1; }

			bool b = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState,kCGMouseButtonLeft);
			CGEventRef e;
			if (b) {
				e = CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDragged,ourLoc,0);
			} else {
				e = CGEventCreateMouseEvent(NULL,kCGEventMouseMoved,ourLoc,0);
			}	
			CGEventPost(kCGSessionEventTap, e);
			CFRelease(e);
			lock_release(lock);
			
			if (command[1] == 'p') {
				lock_acquire(lock);
				ourLoc = CGEventGetLocation(ourEvent);
				lock_release(lock);
				// Instead of being silent, spit out our current position
				char tmp[MAX_PROTOCOL_COMMAND_LEN];
				sprintf(tmp,"%d %d\n",(int)ourLoc.x,(int)ourLoc.y);
				if (writeAll(readfd,tmp,strlen(tmp)) < 0) {
					return -1;
				}
			}
									
#endif
			if (!quiet) {
				printf ("mouse: '%s'\n",command);
			}
			return 1;
		}
		if (!quiet) {
			printf ("swallowed: '%s'\n",command);
		}
		return 1;
	} else if (command[0] == 'm' && command[1] == 'b' && command[2] == ' ') {
		if ((controlMethod & DESKTOP) != 0) {
			int b, p;

#ifdef HAVE_VSTUDIO
			sscanf_s(command, "mb %d,%d", &b, &p);
#else
			sscanf(command, "mb %d,%d", &b, &p);
#endif

#ifdef HAVE_LIBXTST
			if (b >=1 && b <= 5) {
				lock_acquire(lock);
				XTestFakeButtonEvent(linked->dest_display, b, p == 1 ? True : False, 0);
				XFlush(linked->dest_display);
				lock_release(lock);
			}
#endif
#ifdef HAVE_WIN32
			lock_acquire(lock);
			if (b == 1) {
  				if (p == 1) {
     					mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
  				} else {
     					mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
  				}
			} else if (b == 2) {
  				if (p == 1) {
     					mouse_event(MOUSEEVENTF_MIDDLEDOWN,0,0,0,0);
  				} else {
     					mouse_event(MOUSEEVENTF_MIDDLEUP,0,0,0,0);
  				}
			} else if (b == 3) {
  				if (p == 1) {
     					mouse_event(MOUSEEVENTF_RIGHTDOWN,0,0,0,0);
  				} else {
     					mouse_event(MOUSEEVENTF_RIGHTUP,0,0,0,0);
  				}
			} else if (b == 4) {
  				if (p == 1) {
						mouse_event(MOUSEEVENTF_WHEEL,0,0,50,0);
  				} else {
     					mouse_event(MOUSEEVENTF_WHEEL,0,0,50,0);
  				}
			} else if (b == 5) {
  				if (p == 1) {
     					mouse_event(MOUSEEVENTF_WHEEL,0,0,-50,0);
  				} else {
     					mouse_event(MOUSEEVENTF_WHEEL,0,0,-50,0);
  				}
			}
			lock_release(lock);
#endif
#ifdef HAVE_MACOSX
			struct timeval tv;
			gettimeofday(&tv, NULL);
			long nowClick = tv.tv_sec *1000 + tv.tv_usec / 1000;
			lock_acquire(lock);
			CGEventRef e = CGEventCreate(NULL);
			CGPoint ourLoc = CGEventGetLocation(e);
			if (b == 1) {
				CFRelease(e);
				if (p == 1) {
					e = CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDown,ourLoc,kCGMouseButtonLeft);
					long interval = (nowClick - prevClick );
					if (interval < 400) {
						clickCount++;
					} else {
						clickCount = 1;
					}
					eventNumber++;
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
					prevClick = nowClick;
				} else {
					e = CGEventCreateMouseEvent(NULL,kCGEventLeftMouseUp,ourLoc,kCGMouseButtonLeft);
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
				}
				
				
			} else if (b == 2) {
				CFRelease(e);
				if (p == 1) {
					e = CGEventCreateMouseEvent(NULL,kCGEventOtherMouseDown,ourLoc,kCGMouseButtonCenter);
					long interval = (nowClick - prevClick );
					if (interval < 400) {
						clickCount++;
					} else {
						clickCount = 1;
					}
					eventNumber++;
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
					prevClick = nowClick;
				} else {
					e = CGEventCreateMouseEvent(NULL,kCGEventOtherMouseUp,ourLoc,kCGMouseButtonCenter);
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
				}
				
			} else if (b == 3) {
				CFRelease(e);
				if (p == 1) {
					e = CGEventCreateMouseEvent(NULL,kCGEventRightMouseDown,ourLoc,kCGMouseButtonRight);
					long interval = (nowClick - prevClick );
					if (interval < 400) {
						clickCount++;
					} else {
						clickCount = 1;
					}
					eventNumber++;
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
					prevClick = nowClick;
				} else {
					e = CGEventCreateMouseEvent(NULL,kCGEventRightMouseUp,ourLoc,kCGMouseButtonRight);
					CGEventSetIntegerValueField(e, kCGMouseEventClickState, clickCount);
					CGEventSetIntegerValueField (e, kCGMouseEventNumber, eventNumber);
				}
				
			} else if (b == 4) {
				
				CGEventSetType(e, kCGEventScrollWheel);
				CGEventSetIntegerValueField(e, kCGScrollWheelEventInstantMouser, 0);
				
				if (p == 1) {
					CGEventSetIntegerValueField(e, kCGScrollWheelEventDeltaAxis1, 1);
				}
			} else if (b == 5) {
				CGEventSetType(e, kCGEventScrollWheel);
				CGEventSetIntegerValueField(e, kCGScrollWheelEventInstantMouser, 0);
				
				if (p == 1) {
					CGEventSetIntegerValueField(e, kCGScrollWheelEventDeltaAxis1, -1);
				}
			}

			
			CGEventPost(kCGSessionEventTap, e);
			CFRelease(e);
			lock_release(lock);
#endif
			if (!quiet) {
				printf ("mouse: '%s'\n",command);
			}
			return 1;
		}
		if (!quiet) {
			printf ("swallowed: '%s'\n",command);
		}
		return 1;
	}

	if (command[0]=='s' && command[1]=='s' && command[2]==' ') {
		int mode,param1,param2;
#ifdef HAVE_VSTUDIO
		sscanf_s(command,"ss %d %d %d",&mode,&param1,&param2);
#else
		sscanf(command,"ss %d %d %d",&mode,&param1,&param2);
#endif
		// giblib isn't thread safe
		lock_acquire(lock);
		z = snapshot(linked, readfd, mode, param1, param2);
		lock_release(lock);
		return z < 0 ? -1 : 1;
	}

	if (command[0]=='k' && command[1]=='y' && command[2]=='b') {
		char tmp[MAX_PROTOCOL_COMMAND_LEN];
#ifdef HAVE_VSTUDIO
		sscanf_s(command,"kyb %s",tmp,MAX_PROTOCOL_COMMAND_LEN);
#else
		sscanf(command,"kyb %s",tmp);
#endif
		applySymbolTranslation(tmp);
		z = writeKeys(linked,tmp,DOWN_THEN_UP,readfd);
		if (z == 1) {
		    return 1;
		} else if (z == -1) {
		    return -1;
		} else {
		    sprintf_s(tmp2,MAX_PROTOCOL_COMMAND_LEN,"key %s\n",tmp);
		    z = writeMyth(linked,tmp2,readfd);
		    if (z == 1) {
			return 1;
		    } else if (z == -1) {
			return -1;
		    }
		}
	}
	else if (command[0]=='k' && command[1]=='y' && command[2]=='d') {
#ifdef HAVE_VSTUDIO
		sscanf_s(command,"kyd %s",tmp,MAX_PROTOCOL_COMMAND_LEN);
#else
		sscanf(command,"kyd %s",tmp);
#endif
		applySymbolTranslation(tmp);
		z = writeKeys(linked,tmp,DOWN,readfd);
		if (z == 1) {
			return 1;
		} else if (z == -1) {
			return -1;
		}
	}
	else if (command[0]=='k' && command[1]=='y' && command[2]=='u') {
#ifdef HAVE_VSTUDIO
		sscanf_s(command,"kyu %s",tmp,MAX_PROTOCOL_COMMAND_LEN);
#else
		sscanf(command,"kyu %s",tmp);
#endif
		applySymbolTranslation(tmp);
		z = writeKeys(linked,tmp,UP,readfd);
		if (z == 1) {
			return 1;
		} else if (z == -1 ) {
			return -1;
		} else {
			sprintf_s(tmp2,MAX_PROTOCOL_COMMAND_LEN,"key %s\n",tmp);
			z = writeMyth(linked,tmp2,readfd);
			if (z == 1) {
				return 1;
			} else if (z == -1 ){
				return -1;
			}
		}
	}

	// Pass query and play commands to myth
	if (startsWith(command,"play")==1 || startsWith(command,"query")==1) {
		z = writeMyth(linked,command,readfd);
		if (z == 1) {
			return 1;
		} else if (z == -1 ) {
			return -1;
		}
	}

	// If we get here, we should tell client why some commands didn't work
	if (startsWith(command,"query recordings")==1) {
		char *msg = (char*)"0 2000-01-24T00:00:00 Proxy Not Connected To Myth Frontend\n\n";
		if (writeAll(readfd, msg, strlen(msg)) < 0) {
			return -1;
		}
		if (writeAll(readfd, "# ", 2)< 0) {
			return -1;
		}
		return 1;
	}

	if (startsWith(command,"query liveTV")==1) {
		char *msg = (char*) "0 2000-01-24T00:00:00 2000-01-24T00:00:00 Proxy Not Connected To Myth Frontend\n\n";
		if (writeAll(readfd, msg, strlen(msg)) < 0) {
			return -1;
		}
		if (writeAll(readfd, "# ", 2) < 0) {
			return -1;
		}
		return 1;
	}

	// This didn't go anywhere...
#ifdef CLIENT_COMPAT
	if (writeAll(readfd, "OK\n# ", 5) < 0) {
		return -1;
	}
#endif
	if (!quiet) {
		printf ("swallowed: '%s'\n",command);
	}
	return 1;
}

void readAndDispatch(void *data)
{
	linkedcons_t linked = (linkedcons_t) data;
	int    res;
	SOCKET_T maxfd;
    fd_set fds;
    struct timeval tv;
	int st;
	char buf[MAX_PROTOCOL_COMMAND_LEN];

	// Only one of these will be active at a time...
	SOCKET_T readfd;
	if (linked->soinb == NULL) {
        	readfd = linked->btinb->btfd;
	} else {
        	readfd = linked->soinb->sofd;
	}

	while (linked->inbConnected == 1) {

	    FD_ZERO(&fds);
	    FD_SET((unsigned int)readfd,&fds); maxfd = readfd;
	    tv.tv_sec = 1;
	    tv.tv_usec = 0;

		res = select(maxfd+1, &fds, (fd_set *) 0, (fd_set *) 0, (struct timeval *) &tv);

		if (res == -1) {
			printf("select error\n");
			break;
		} else if (res) {
			st = readLine(buf, readfd);
			if (st > 0) {
				int z = processCommand(buf, linked, readfd);
				if (z > 0) {
					// handled ok
					continue;
				} else if (z == -1) {
					// network error, just hang up
					break;
				}
			}
			// 0 from process command or read error
			// Force reconnect from client
			writeAll(readfd,"ERROR\n# ",8);
			break;
		}
	}

	if (!quiet) {
		printf("inbp exiting..\n");
	}
}

socon_t waitForSocketConnection() {
	socon_t socon;
	int flag;
	struct timeval tv;
	SOCKET_T fd;
#ifdef HAVE_VSTUDIO
	SOCKADDR_IN loc_addr;
	SOCKADDR_IN rem_addr;
	int opt;
#else
	socklen_t opt;
	struct sockaddr_in rem_addr = { 0 };
	struct sockaddr_in loc_addr = { 0 };
#endif
	opt = sizeof(rem_addr);

    if (bound == 0) {

#ifdef HAVE_VSTUDIO
		inbs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (inbs == INVALID_SOCKET) {
			printf("Error at socket(): %ld\n", WSAGetLastError());
			return NULL;
		}
		loc_addr.sin_family = AF_INET;
		loc_addr.sin_addr.s_addr = INADDR_ANY;
		loc_addr.sin_port = htons(acceptPort);

		if (bind( inbs, (SOCKADDR*) &loc_addr, opt) == SOCKET_ERROR) {
			setState(FREEMOTED_STATUS_FAILED,(void*)FREEMOTED_FAILURE_BIND);
			printf("bind() failed.\n");
			closesocket(inbs);
			inbs = INVALID_SOCKET;
			return NULL;
		}

		if (listen( inbs, SOMAXCONN ) == SOCKET_ERROR) {
			return NULL;
		}
#else
	    loc_addr.sin_family = AF_INET;
	    loc_addr.sin_addr.s_addr = INADDR_ANY;
	    loc_addr.sin_port = htons(acceptPort);

	    if (bind(inbs, (struct sockaddr *) &loc_addr, sizeof(loc_addr)) == -1) {
			setState(FREEMOTED_STATUS_FAILED,(void*)FREEMOTED_FAILURE_BIND);
			closesocket(inbs);
			inbs = 0;
			return NULL;
		}

	    // put socket into listening mode
	    listen(inbs, 1);
#endif
	    bound = 1;
	}


	if (!quiet) {
		printf("Waiting for socket connection on port %d...\n", acceptPort);
	}
#ifdef HAVE_VSTUDIO
	fd = accept(inbs, (SOCKADDR*) &rem_addr, &opt);
	if (fd == INVALID_SOCKET) {
		printf("accept failed: %d\n", WSAGetLastError());
		closesocket(inbs);
		inbs = INVALID_SOCKET;
		return NULL;
	}
#else
	// accept one connection
	fd = accept(inbs, (struct sockaddr *) &rem_addr, &opt);
#endif

	if (inbs == 0) {
		return NULL;
	}

	if (!quiet) {
		printf("Accepted connection from %s\n", (char *) inet_ntoa(
				rem_addr.sin_addr));
	}

	socon = (socon_t) malloc(sizeof(socon));
	socon->sofd = fd;
	flag = 1;
	setsockopt(socon->sofd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

	// Give up trying to write after 20 seconds
	tv.tv_sec = 20;
	tv.tv_usec = 0;

	setsockopt(socon->sofd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));

	return socon;
}

#ifdef HAVE_VSTUDIO
DEFINE_GUID(SAMPLE_UUID, 0x31b44148, 0x041f, 0x42f5, 0x8e, 0x73, 0x18, 0x6d, 0x5a, 0x47, 0x9f, 0xc9);
#endif

btcon_t waitForBluetoothConnection() {
#ifdef HAVE_VSTUDIO

	SOCKADDR_BTH sa2;
	int size = sizeof(sa2);
	SOCKET s2;
	btcon_t btcon;
	CSADDR_INFO sockInfo;
	WSAQUERYSET serviceInfo = { 0 };

	if (bound ==0) {
		SOCKADDR_BTH loc_addr = {0};
		int loc_addr_len = sizeof(loc_addr);

		loc_addr.addressFamily = AF_BTH;
		loc_addr.btAddr = 0;
		loc_addr.serviceClassId = SerialPortServiceClass_UUID;
		if (btRegisteredThisProcess == 0) {
			loc_addr.port = BT_PORT_ANY;
		} else {
			loc_addr.port = btChannel;
		}

		if (bind(inbs, (SOCKADDR *) &loc_addr, sizeof(loc_addr)) == SOCKET_ERROR) {
			closesocket(inbs);
			setState(FREEMOTED_STATUS_FAILED,(void*)FREEMOTED_FAILURE_BIND_BT);
			inbs = INVALID_SOCKET;
			return NULL;
		}

		listen(inbs,5);

		// check which port we’re listening on
		if( SOCKET_ERROR == getsockname( inbs, (SOCKADDR*) &loc_addr, &loc_addr_len ) ) {
			closesocket(inbs);
			setState(FREEMOTED_STATUS_FAILED,(void*)FREEMOTED_FAILURE_BIND_BT);
			inbs = INVALID_SOCKET;
			return NULL;
		}

		if (btRegisteredThisProcess == 0) {
			// remember the channel that was chosen, we're gonna reserve it for the lifetime of this process
			btChannel = (int)loc_addr.port;

			// advertise the service
			sockInfo.iProtocol = BTHPROTO_RFCOMM;
			sockInfo.iSocketType = SOCK_STREAM;
			sockInfo.LocalAddr.lpSockaddr = (LPSOCKADDR) &loc_addr;
			sockInfo.LocalAddr.iSockaddrLength = sizeof(loc_addr);
			sockInfo.RemoteAddr.lpSockaddr = (LPSOCKADDR) &loc_addr;
			sockInfo.RemoteAddr.iSockaddrLength = sizeof(loc_addr);
			serviceInfo.dwSize = sizeof(serviceInfo);
			serviceInfo.dwNameSpace = NS_BTH;
			serviceInfo.lpszServiceInstanceName = TEXT("Freemote Server");
			serviceInfo.lpszComment = TEXT("Serial Port");
			serviceInfo.lpServiceClassId = (LPGUID) &SerialPortServiceClass_UUID;
			serviceInfo.dwNumberOfCsAddrs = 1;
			serviceInfo.lpcsaBuffer = &sockInfo;
			if( SOCKET_ERROR ==
				WSASetService( &serviceInfo, RNRSERVICE_REGISTER, 0 ) ) {
				closesocket(inbs);
				inbs = INVALID_SOCKET;
				return NULL;
			}
			btRegisteredThisProcess = 1;
		}

		bound = 1;
	}

	s2 = accept (inbs, (SOCKADDR *)&sa2, &size);

	btcon = (btcon_t) malloc(sizeof(btcon));
	btcon->btfd = s2;

	return btcon;

#endif
#ifdef HAVE_LIBBLUETOOTH
    if (bound ==0) {

	    bdaddr_t ANYADDR;
	    ANYADDR.b[0] = 0;
	    ANYADDR.b[1] = 0;
	    ANYADDR.b[2] = 0;
	    ANYADDR.b[3] = 0;
	    ANYADDR.b[4] = 0;
	    ANYADDR.b[5] = 0;

	    struct sockaddr_rc loc_addr = { 0 };

	    loc_addr.rc_family = AF_BLUETOOTH;
	    loc_addr.rc_bdaddr = ANYADDR;
	    loc_addr.rc_channel = (uint8_t) btChannel;
	    bind(inbs, (struct sockaddr *) &loc_addr, sizeof(loc_addr));

	    // put socket into listening mode
	    listen(inbs, 1);
	    bound = 1;
	}


	struct sockaddr_rc rem_addr = { 0 };
	int client;
	socklen_t opt = sizeof(rem_addr);
	char buf[1024] = { 0 };

	if (!quiet) {
		printf("Waiting for bluetooth connection on serial port...\n");
	}
	// accept one connection
	client = accept(inbs, (struct sockaddr *) &rem_addr, &opt);

	if (inbs == 0) {
		return NULL;
	}

	ba2str(&rem_addr.rc_bdaddr, buf);

	if (rem_addr.rc_bdaddr.b[0] == 0 && rem_addr.rc_bdaddr.b[1] == 0
			&& rem_addr.rc_bdaddr.b[2] == 0 && rem_addr.rc_bdaddr.b[3] == 0
			&& rem_addr.rc_bdaddr.b[4] == 0 && rem_addr.rc_bdaddr.b[5] == 0) {
		closesocket(inbs);
		return NULL;
	}

	if (!quiet) {
		printf("Accepted connection from %s\n", buf);
	}
	memset(buf, 0, sizeof(buf));

	btcon_t btcon = (btcon_t) malloc(sizeof(btcon));
	btcon->btfd = client;

	return btcon;
#else
	return NULL;
#endif
}

socon_t connectToSocket(char *hostName, int hostPort) {
	SOCKET_T sockfd;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	socon_t socon;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return NULL;
	server = gethostbyname(hostName);
	if (server == NULL) {
		fprintf(stderr, "ERROR, no such host %s\n", hostName);
		return NULL;
	}
#ifdef HAVE_VSTUDIO
	ZeroMemory(&serv_addr, sizeof (serv_addr));
#else
	bzero((char *) &serv_addr, sizeof(serv_addr));
#endif

	serv_addr.sin_family = AF_INET;
#ifdef HAVE_VSTUDIO
	CopyMemory((char *) &serv_addr.sin_addr.s_addr,(char *) server->h_addr,
			server->h_length);
#else
	bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr,
			server->h_length);
#endif
	serv_addr.sin_port = htons(hostPort);
	if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
		return NULL;

	if (!quiet) {
		printf("Connected to %s:%d\n", hostName, hostPort);
	}

	socon = (socon_t) malloc(sizeof(socon));
	socon->sofd = sockfd;
	return socon;
}

void connectToMyth(linkedcons_t linked) {
	struct timeval tv;

	// Shutdown previous connection if there is one
	if (linked->mythSocketCon != NULL) {
		closesocket(linked->mythSocketCon->sofd);
		free(linked->mythSocketCon);
		linked->mythSocketCon = NULL;
	}

	linked->disableMyth = 0;
	if ((controlMethod & MYTHFE) != 0) {
		linked->mythSocketCon = connectToSocket(mythHost, mythPort);

		// Short timeout when we're waiting for data from myth
		tv.tv_sec = 3;
		tv.tv_usec = 0;

		if (linked->mythSocketCon == NULL) {
			fprintf(stderr, "Could not connect to myth front end server %s:%d\n",mythHost,mythPort);
			linked->disableMyth = 1;
			return;
		}

		setsockopt(linked->mythSocketCon->sofd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

		if (consumeUntilPrompt(linked) == 0) {
			linked->disableMyth = 1;
			return;
		}
	}
}

void handleConnection(void *data) {
	SOCKET_T readfd;
	linkedcons_t linked = (linkedcons_t) data;
	char buf[32];
	int pos = 0;
	char* verStr = (char*)"ver:2\n";
	char tmp[MAX_PROTOCOL_COMMAND_LEN];
	
	// Connect to myth socket server
	connectToMyth(linked);

	// Only one of these will be active at a time...
	
	if (linked->soinb == NULL) {
        	readfd = linked->btinb->btfd;
	} else {
        	readfd = linked->soinb->sofd;
	}

	buf[pos++] = 'c'; buf[pos++] = 'a'; buf[pos++] = 'p'; buf[pos++] = ':';
	if ((controlMethod & MYTHFE) != 0) {
		buf[pos++] = 'm'; buf[pos++] = ','; // myth network control
	}
	if ((controlMethod & MYTHDB) != 0) {
		buf[pos++] = 'd'; buf[pos++] = ','; // myth database connection
	}
	if ((controlMethod & DESKTOP) != 0) {
		buf[pos++] = 'x'; buf[pos++] = ','; // desktop key/mouse events
#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
		buf[pos++] = 's'; buf[pos++] = ','; // screen capture
#endif
#endif
	}
	if ((controlMethod & LIRC) != 0) {
		buf[pos++] = 'l'; buf[pos++] = ','; // lirc simulated events
	}
	buf[pos++] = 'k'; buf[pos++] = ','; // we support the kyb command
	buf[pos++] = '\n';
	buf[pos++] = '\0';

	if (writeAll(readfd,buf,strlen(buf)) < 0) {
		cleanup(linked);
		return;
	}
	if (writeAll(readfd,verStr,strlen(verStr)) < 0) {
		cleanup(linked);
		return;
	}

	if ((controlMethod & DESKTOP) != 0) {
		sprintf_s(tmp,MAX_PROTOCOL_COMMAND_LEN,"scr:%d %d\n",linked->displayWidth,linked->displayHeight);
		if (writeAll(readfd,tmp,strlen(tmp)) < 0) {
			cleanup(linked);
			return;
		}
		sprintf_s(tmp,MAX_PROTOCOL_COMMAND_LEN,"chk:%d %d\n",MAX_SCREENSHOT_CHUNK_WIDTH,MAX_SCREENSHOT_CHUNK_HEIGHT);
		if (writeAll(readfd,tmp,strlen(tmp)) < 0) {
			cleanup(linked);
			return;
		}
	}

#ifdef CLIENT_COMPAT
	if (writeAll(readfd,"# ",2) < 0) {
		cleanup(linked);
		return;
	}
#endif

	readAndDispatch(linked);

	cleanup(linked);

	if (!quiet) {
		printf("inbound connection closed & cleaned up\n");
	}
}

void usage(char *name) {
	printf("Usage:\n   %s -t [bluetooth|socket] [-t [-m myth|lirc|database|desktop] [-v] [-help] [others (see below)]\n\n",
			name);
	printf("Options:\n");
	printf("      -help prints usage\n");
	printf("      -v turns on verbose logging to stdout\n");
	printf("      -t sets inbound connection method, socket or bluetooth (default socket)\n");
	printf("\n");
	printf("   inbound socket connection options\n");
	printf("      -l sets the listen port (default 8001)\n");
	printf("\n");
	printf("   inbound bluetooth connection options\n");
	printf("      -c sets the bluetooth channel (default 1)\n");
	printf("\n");
	printf("   control method options\n");
	printf("      -m myth|lirc|database|desktop (default desktop)\n");
	printf("      (use -m nodesktop to disable mouse/keyboard control)\n");
	printf("\n");
	printf("      Myth Frontend control method options\n");
	printf("      -p sets the myth port number (default 6546)\n");
	printf("      -h sets the myth host address (default localhost)\n");
	printf("\n");
	printf("      Lirc control method options\n");
	printf("      -i lirc host (default localhost)\n");
	printf("      -j lirc port (default 8888)\n");
	printf("\n");
#ifdef HAVE_LIBXTST
	printf("      Desktop control method options\n");
	printf("      -x X display string (default :0.0)\n");
	printf("\n");
#endif
	printf("      Myth database control method options\n");
	printf("      -s mysql host (default localhost)\n");
	printf("      -o mysql port (default 3306)\n");
	printf("      -u mysql user (default mythtv)\n");
	printf("      -a mysql password (default mythtv)\n");
	printf("\n");
#ifdef HAVE_LIBBLUETOOTH
	printf("   If using bluetooth, use sdptool to register the \n");
	printf("   serial port on chosen channel.  This is required \n");
	printf("   once at system startup.\n");
	printf("       i.e. sdptool add --channel 1 SP\n");
	printf("\n");
#endif
	printf("   Lirc requires --allow-simulate and --listen options\n");
	printf("   on lircd.\n");
	printf("\n");
#ifdef HAVE_LIBXTST
	printf("   XWindows server must support the XTest extension.\n");
	printf("\n");
#endif
}

int parseOptions(int argc, char **argv) {
	int a = 1;

	if (argc == 1) {
		return 0;
	}

	while (a < argc) {
		if (strcmp(argv[a], "-help") == 0) {
			usage(argv[0]);
			return 1;
		} else if (strcmp(argv[a], "-p") == 0) {
			a++;
			if (a >= argc) {
				printf("-p requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mythPort = atoi(argv[a]);
		} else if (strcmp(argv[a], "-c") == 0) {
			a++;
			if (a >= argc) {
				printf("-c requires argument\n");
				usage(argv[0]);
				return 1;
			}
			btChannel = atoi(argv[a]);
		} else if (strcmp(argv[a], "-l") == 0) {
			a++;
			if (a >= argc) {
				printf("-l requires argument\n");
				usage(argv[0]);
				return 1;
			}
			acceptPort = atoi(argv[a]);
		} else if (strcmp(argv[a], "-h") == 0) {
			a++;
			if (a >= argc) {
				printf("-h requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mythHost = strdup(argv[a]);
		} else if (strcmp(argv[a], "-t") == 0) {
			a++;
			if (a >= argc) {
				printf("-t requires argument\n");
				usage(argv[0]);
				return 1;
			}
			if (strcasecmp(argv[a], "bluetooth")==0) {
				proxyType = CT_BLUETOOTH;
			}
			else if (strcasecmp(argv[a], "socket")==0) {
				proxyType = CT_SOCKET;
			} else {
				printf("Unrecognized argument for -t\n");
				usage(argv[0]);
				return 1;
			}
		} else if (strcmp(argv[a], "-m") == 0) {
			a++;
			if (a >= argc) {
				printf("-m requires argument\n");
				usage(argv[0]);
				return 1;
			}
			if (strcasecmp(argv[a], "lirc")==0) {
				controlMethod |= LIRC;
			} else if (strcasecmp(argv[a], "myth")==0) {
				controlMethod |= MYTHFE;
			} else if (strcasecmp(argv[a], "database")==0) {
				controlMethod |= MYTHDB;
			} else if (strcasecmp(argv[a], "desktop")==0) {
				controlMethod |= DESKTOP;
			} else if (strcasecmp(argv[a], "nodesktop")==0) {
				controlMethod &= ~DESKTOP;
			}
		} else if (strcmp(argv[a], "-s") == 0) {
			a++;
			if (a >= argc) {
				printf("-s requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mysqlHost = strdup(argv[a]);
		} else if (strcmp(argv[a], "-o") == 0) {
			a++;
			if (a >= argc) {
				printf("-o requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mysqlPort = atoi(argv[a]);
		} else if (strcmp(argv[a], "-u") == 0) {
			a++;
			if (a >= argc) {
				printf("-u requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mysqlUser = strdup(argv[a]);
		} else if (strcmp(argv[a], "-a") == 0) {
			a++;
			if (a >= argc) {
				printf("-a requires argument\n");
				usage(argv[0]);
				return 1;
			}
			mysqlPassword = strdup(argv[a]);
		} else if (strcmp(argv[a], "-x") == 0) {
			a++;
			if (a >= argc) {
				printf("-x requires argument\n");
				usage(argv[0]);
				return 1;
			}
			xDisplay = strdup(argv[a]);
		} else if (strcmp(argv[a], "-i") == 0) {
			a++;
			if (a >= argc) {
				printf("-i requires argument\n");
				usage(argv[0]);
				return 1;
			}
			lircHost = strdup(argv[a]);
		} else if (strcmp(argv[a], "-j") == 0) {
			a++;
			if (a >= argc) {
				printf("-j requires argument\n");
				usage(argv[0]);
				return 1;
			}
			lircPort = atoi(argv[a]);
		} else if (strcmp(argv[a], "-v") == 0) {
			quiet = 0;
		} else {
			fprintf(stderr, "Unknown option %s\n", argv[a]);
			usage(argv[0]);
			return 1;
		}
		a++;
	}
	return 0;
}

void nolinger(SOCKET_T sock) {
	static struct linger linger = { 0, 0 };
	int lsize = sizeof(struct linger);
	setsockopt(sock, SOL_SOCKET, SO_LINGER, (void *) &linger, lsize);
}

int checkArgs() {
	if (controlMethod == 0) {
		printf ("WARNING: There are no control methods specified!\n");
		printf ("WARNING: No events will be proxied anywhere.\n\n");
	}
	return 0;
}

// Just read and throw away whatever lircd give us, we don't care
void consumeLirc(void* data) {
	linkedcons_t linked = (linkedcons_t) data;
	int    res;
	SOCKET_T maxfd;
    fd_set fds;
    struct timeval tv;

	char buf[1024];
	while (linked->lircConnected == 1) {

	    FD_ZERO(&fds);
	    FD_SET((unsigned int)linked->solirc->sofd,&fds); maxfd = linked->solirc->sofd;
	    tv.tv_sec = 1;
	    tv.tv_usec = 0;

		res = select(maxfd+1, &fds, (fd_set *) 0, (fd_set *) 0, (struct timeval *) &tv);

		if (res == -1) {
			printf ("select error\n");
			break;
		} else if (res) {
			if( FD_ISSET(linked->solirc->sofd, &fds)) {
				int st = readLine(buf, linked->solirc->sofd);
				if (st <= 0) {
					break;
				}
			}
		}
	}

	if (!quiet) {
		printf ("lirc consumer exiting\n");
	}

}

#ifdef HAVE_MACOSX
CGKeyCode keyCodeForCharWithLayout(const char c,
                                   const UCKeyboardLayout *uchrHeader);

CGKeyCode keyCodeForChar(const char c)
{
    CFDataRef currentLayoutData;
    TISInputSourceRef currentKeyboard = TISCopyCurrentKeyboardInputSource();
	
    if (currentKeyboard == NULL) {
        fputs("Could not find keyboard layout\n", stderr);
        return UINT16_MAX;
    }
	
    currentLayoutData = TISGetInputSourceProperty(currentKeyboard,
												  kTISPropertyUnicodeKeyLayoutData);
    CFRelease(currentKeyboard);
    if (currentLayoutData == NULL) {
        fputs("Could not find layout data\n", stderr);
        return UINT16_MAX;
    }
	
    return keyCodeForCharWithLayout(c,
									(const UCKeyboardLayout *)CFDataGetBytePtr(currentLayoutData));
}

/* Beware! Messy, incomprehensible code ahead!
 * TODO: XXX: FIXME! Please! */
CGKeyCode keyCodeForCharWithLayout(const char c,
                                   const UCKeyboardLayout *uchrHeader)
{
    uint8_t *uchrData = (uint8_t *)uchrHeader;
    UCKeyboardTypeHeader *uchrKeyboardList = uchrHeader->keyboardTypeList;
	
    /* Loop through the keyboard type list. */
    ItemCount i, j;
    for (i = 0; i < uchrHeader->keyboardTypeCount; ++i) {
        /* Get a pointer to the keyToCharTable structure. */
        UCKeyToCharTableIndex *uchrKeyIX = (UCKeyToCharTableIndex *)
        (uchrData + (uchrKeyboardList[i].keyToCharTableIndexOffset));
		
        /* Not sure what this is for but it appears to be a safeguard... */
        UCKeyStateRecordsIndex *stateRecordsIndex;
        if (uchrKeyboardList[i].keyStateRecordsIndexOffset != 0) {
			stateRecordsIndex = (UCKeyStateRecordsIndex *)
			(uchrData + (uchrKeyboardList[i].keyStateRecordsIndexOffset));
			
			if ((stateRecordsIndex->keyStateRecordsIndexFormat) !=
				kUCKeyStateRecordsIndexFormat) {
				stateRecordsIndex = NULL;
			}
        } else {
			stateRecordsIndex = NULL;
        }
		
        /* Make sure structure is a table that can be searched. */
        if ((uchrKeyIX->keyToCharTableIndexFormat) != kUCKeyToCharTableIndexFormat) {
			continue;
        }
		
        /* Check the table of each keyboard for character */
        for (j = 0; j < uchrKeyIX->keyToCharTableCount; ++j) {
			UCKeyOutput *keyToCharData =
			(UCKeyOutput *)(uchrData + (uchrKeyIX->keyToCharTableOffsets[j]));
			
			/* Check THIS table of the keyboard for the character. */
			UInt16 k;
			for (k = 0; k < uchrKeyIX->keyToCharTableSize; ++k) {
				/* Here's the strange safeguard again... */
				if ((keyToCharData[k] & kUCKeyOutputTestForIndexMask) ==
					kUCKeyOutputStateIndexMask) {
					long keyIndex = (keyToCharData[k] & kUCKeyOutputGetIndexMask);
					if (stateRecordsIndex != NULL &&
						keyIndex <= (stateRecordsIndex->keyStateRecordCount)) {
						UCKeyStateRecord *stateRecord = (UCKeyStateRecord *)
						(uchrData +
						 (stateRecordsIndex->keyStateRecordOffsets[keyIndex]));
						
						if ((stateRecord->stateZeroCharData) == c) {
							return (CGKeyCode)k;
						}
					} else if (keyToCharData[k] == c) {
						return (CGKeyCode)k;
					}
				} else if (((keyToCharData[k] & kUCKeyOutputTestForIndexMask)
							!= kUCKeyOutputSequenceIndexMask) &&
						   keyToCharData[k] != 0xFFFE &&
						   keyToCharData[k] != 0xFFFF &&
						   keyToCharData[k] == c) {
					return (CGKeyCode)k;
				}
			}
        }
    }
	
    return UINT16_MAX;
}
#endif

int readConfig() {
	FILE *fp;
	char line[MAX_CONFIG_LINE_LEN];
    	char file[MAX_FILENAME_LEN];
	char tmp[MAX_DIRECTIVES_PER_COMMAND][MAX_CONFIG_DIRECTIVE_LEN];
	char *n;
	int o;
	size_t l;
	int num;
	int skdest1;
	int skdest2;

	// Open config file
#ifdef HAVE_VSTUDIO
	if (configFile != NULL) {
		fp = _wfopen(configFile, TEXT("r"));
	} else {
		fp = _wfopen(TEXT("freemoted.conf"), TEXT("r"));
	}
#else
	if (configFile != NULL) {
		sprintf_s (file,MAX_FILENAME_LEN,"%s",configFile);
	}
	else if (strcmp("",QUOTED_SYSCONFDIR(SYSCONFDIR)) == 0) {
	    sprintf_s (file,MAX_FILENAME_LEN,"/etc/freemoted.conf");
 	}
	else {
	    sprintf_s (file,MAX_FILENAME_LEN,"%s/freemoted.conf",QUOTED_SYSCONFDIR(SYSCONFDIR));
	}

	fp = fopen(file,"r");
#endif
	if (fp == NULL) {
#ifdef HAVE_VSTUDIO
		return 0;
#else
		// Try current dir
		fp = fopen("./freemoted.conf","r");
#endif
		if (fp == NULL) {
			fprintf(stderr,"Can't open %s\n",file);
			return 0;
		}
	}

	numKeyCmds = 0;
	while (!feof(fp)) {
		n = fgets(line, sizeof line, fp);
		if (!feof(fp) && strlen(line) > 0) {
			line[strlen(line) - 1] = '\0';
			if (line[0] == '#' || line[0] == '\0') {
				continue;
			}
			else if (line[0] == 'k' && line[1] == 'y' && line[2] == 'b' && line[3] == ' ') {
				strcpy_s(tmp[0],MAX_CONFIG_DIRECTIVE_LEN,"\0");
				skdest1 = numTranslatedSymbols;
				skdest2 = numTranslatedSymbols+1;
#ifdef HAVE_VSTUDIO
				sscanf_s( line, "%[^ ] %[^ ] %[^ ]",
					tmp[0],MAX_CONFIG_DIRECTIVE_LEN,
					translatedSymbols[skdest1],MAX_CONFIG_DIRECTIVE_LEN,
					translatedSymbols[skdest2],MAX_CONFIG_DIRECTIVE_LEN);
#else
				sscanf( line, "%[^ ] %[^ ] %[^ ]",
					tmp[0],
					translatedSymbols[skdest1],
					translatedSymbols[skdest2]);
#endif
				numTranslatedSymbols+=2;
			}
			else if (line[0] == 'b' && line[1] == 't' && line[2] == 'n' && line[3] == ' ') {
				strcpy_s(tmp[0],MAX_CONFIG_DIRECTIVE_LEN,"\0");
				strcpy_s(tmp[1],MAX_CONFIG_DIRECTIVE_LEN,"\0");
				strcpy_s(tmp[2],MAX_CONFIG_DIRECTIVE_LEN,"\0");
#ifdef HAVE_VSTUDIO
				sscanf_s( line, "%[^,],%[^,],%[^,],%[^,]",
						pxyCmd[numKeyCmds],MAX_CONFIG_DIRECTIVE_LEN,
						tmp[0],MAX_CONFIG_DIRECTIVE_LEN,tmp[1],MAX_CONFIG_DIRECTIVE_LEN,tmp[2],MAX_CONFIG_DIRECTIVE_LEN);
#else
				sscanf( line, "%[^,],%[^,],%[^,],%[^,]",
						pxyCmd[numKeyCmds],
						tmp[0],tmp[1],tmp[2]);
#endif
				for (o=0;o<MAX_DIRECTIVES_PER_COMMAND;o++) {
					if (tmp[o][0]=='l') {
#ifdef HAVE_VSTUDIO
						sscanf_s(tmp[o],"lirc(%[^)]",lircCmd[numKeyCmds],MAX_CONFIG_DIRECTIVE_LEN);
#else
						sscanf(tmp[o],"lirc(%[^)]",lircCmd[numKeyCmds]);
#endif
						cmdOrder[numKeyCmds][o]=LIRC;
					}
					else if (tmp[o][0]=='k') {
#ifdef HAVE_VSTUDIO
						sscanf_s(tmp[o],"key(%[^)]",keyCmd[numKeyCmds],MAX_CONFIG_DIRECTIVE_LEN);
#else
						sscanf(tmp[o],"key(%[^)]",keyCmd[numKeyCmds]);
#endif
						cmdOrder[numKeyCmds][o]=DESKTOP;
					}
					else if (tmp[o][0]=='s') {
#ifdef HAVE_VSTUDIO
						sscanf_s(tmp[o],"shell(%[^)]",shellCmd[numKeyCmds],MAX_CONFIG_DIRECTIVE_LEN);
#else
						sscanf(tmp[o],"shell(%[^)]",shellCmd[numKeyCmds]);
#endif
						cmdOrder[numKeyCmds][o]=SHELL;
					}
					else if (tmp[o][0]=='m') {
#ifdef HAVE_VSTUDIO
						sscanf_s(tmp[o],"myth(%[^)]",mythCmd[numKeyCmds],MAX_CONFIG_DIRECTIVE_LEN);
#else
						sscanf(tmp[o],"myth(%[^)]",mythCmd[numKeyCmds]);
#endif
						l = strlen(mythCmd[numKeyCmds]);
						mythCmd[numKeyCmds][l] = '\n';
						mythCmd[numKeyCmds][l+1] = '\0';
						cmdOrder[numKeyCmds][o]=MYTHFE;
					}
					else if (tmp[o][0]=='\0') {
						// empty, ignore
					}
					else {
						printf ("Unknown directive in freemoted.conf\n");
					}
				}
				numKeyCmds++;
			}
		}
	}

//	for (int i=0;i<numKeyCmds;i++) {
//		printf ("%s,%s,%s,%s\n",trim(pxyCmd[i]),trim(lircCmd[i]),trim(keyCmd[i]),trim(keyCmd[i]));
//	}

	fclose(fp);

	num = 0;
#ifdef HAVE_LIBXTST
	strcpy(keyCodes[num],"mute"); keySyms[num++] = XF86XK_AudioMute;
	strcpy(keyCodes[num],"voldown"); keySyms[num++] = XF86XK_AudioLowerVolume;
	strcpy(keyCodes[num],"volup"); keySyms[num++] = XF86XK_AudioRaiseVolume;
	strcpy(keyCodes[num],"mediaplay"); keySyms[num++] = XF86XK_AudioPlay;
	strcpy(keyCodes[num],"mediastop"); keySyms[num++] = XF86XK_AudioStop;
	strcpy(keyCodes[num],"mediaprev"); keySyms[num++] = XF86XK_AudioPrev;
	strcpy(keyCodes[num],"medianext"); keySyms[num++] = XF86XK_AudioNext;
	strcpy(keyCodes[num],"enter"); keySyms[num++] = XK_Return;
	strcpy(keyCodes[num],"backspace"); keySyms[num++] = XK_BackSpace;
	strcpy(keyCodes[num],"tab"); keySyms[num++] = XK_Tab;
	strcpy(keyCodes[num],"space"); keySyms[num++] = XK_space;
	strcpy(keyCodes[num],"escape"); keySyms[num++] = XK_Escape;
	strcpy(keyCodes[num],"up"); keySyms[num++] = XK_Up;
	strcpy(keyCodes[num],"down"); keySyms[num++] = XK_Down;
	strcpy(keyCodes[num],"left"); keySyms[num++] = XK_Left;
	strcpy(keyCodes[num],"right"); keySyms[num++] = XK_Right;
	strcpy(keyCodes[num],"alt"); keySyms[num++] = XK_Alt_L;
	strcpy(keyCodes[num],"shift"); keySyms[num++] = XK_Shift_L;
	strcpy(keyCodes[num],"cntrl"); keySyms[num++] = XK_Control_L;
	strcpy(keyCodes[num],"F1"); keySyms[num++] = XK_F1;
	strcpy(keyCodes[num],"F2"); keySyms[num++] = XK_F2;
	strcpy(keyCodes[num],"F3"); keySyms[num++] = XK_F3;
	strcpy(keyCodes[num],"F4"); keySyms[num++] = XK_F4;
	strcpy(keyCodes[num],"F5"); keySyms[num++] = XK_F5;
	strcpy(keyCodes[num],"F6"); keySyms[num++] = XK_F6;
	strcpy(keyCodes[num],"F7"); keySyms[num++] = XK_F7;
	strcpy(keyCodes[num],"F8"); keySyms[num++] = XK_F8;
	strcpy(keyCodes[num],"F9"); keySyms[num++] = XK_F9;
	strcpy(keyCodes[num],"F10"); keySyms[num++] = XK_F10;
	strcpy(keyCodes[num],"F11"); keySyms[num++] = XK_F11;
	strcpy(keyCodes[num],"F12"); keySyms[num++] = XK_F12;
	strcpy(keyCodes[num],"home"); keySyms[num++] = XK_Home;
	strcpy(keyCodes[num],"end"); keySyms[num++] = XK_End;
	strcpy(keyCodes[num],"'"); keySyms[num++] = XK_apostrophe;
	strcpy(keyCodes[num],"apostrophe"); keySyms[num++] = XK_apostrophe;
	strcpy(keyCodes[num],","); keySyms[num++] = XK_comma;
	strcpy(keyCodes[num],"comma"); keySyms[num++] = XK_comma;
	strcpy(keyCodes[num],"-"); keySyms[num++] = XK_minus;
	strcpy(keyCodes[num],"minus"); keySyms[num++] = XK_minus;
	strcpy(keyCodes[num],"."); keySyms[num++] = XK_period;
	strcpy(keyCodes[num],"period"); keySyms[num++] = XK_period;
	strcpy(keyCodes[num],"/"); keySyms[num++] = XK_slash;
	strcpy(keyCodes[num],"slash"); keySyms[num++] = XK_slash;
	strcpy(keyCodes[num],";"); keySyms[num++] = XK_semicolon;
	strcpy(keyCodes[num],"semicolon"); keySyms[num++] = XK_semicolon;
	strcpy(keyCodes[num],"\\"); keySyms[num++] = XK_backslash;
	strcpy(keyCodes[num],"backslash"); keySyms[num++] = XK_backslash;
	strcpy(keyCodes[num],"`"); keySyms[num++] = XK_grave;
	strcpy(keyCodes[num],"grave"); keySyms[num++] = XK_grave;
	strcpy(keyCodes[num],"del"); keySyms[num++] = XK_Delete;
	strcpy(keyCodes[num],"pageup"); keySyms[num++] = XK_Page_Up;
	strcpy(keyCodes[num],"pagedown"); keySyms[num++] = XK_Page_Down;
	strcpy(keyCodes[num],"numbersign"); keySyms[num++] = XK_numbersign;
	strcpy(keyCodes[num],"dollar"); keySyms[num++] = XK_dollar;
	strcpy(keyCodes[num],"percent"); keySyms[num++] = XK_percent;
	strcpy(keyCodes[num],"ampersand"); keySyms[num++] = XK_ampersand;
	strcpy(keyCodes[num],"asterisk"); keySyms[num++] = XK_asterisk;
	strcpy(keyCodes[num],"plus"); keySyms[num++] = XK_plus;
	strcpy(keyCodes[num],"colon"); keySyms[num++] = XK_colon;
	strcpy(keyCodes[num],"less"); keySyms[num++] = XK_less;
	strcpy(keyCodes[num],"greater"); keySyms[num++] = XK_greater;
	strcpy(keyCodes[num],"question"); keySyms[num++] = XK_question;
	strcpy(keyCodes[num],"at"); keySyms[num++] = XK_at;
	strcpy(keyCodes[num],"bracketleft"); keySyms[num++] = XK_bracketleft;
	strcpy(keyCodes[num],"backslash"); keySyms[num++] = XK_backslash;
	strcpy(keyCodes[num],"bracketright"); keySyms[num++] = XK_bracketright;
	strcpy(keyCodes[num],"asciicircum"); keySyms[num++] = XK_asciicircum;
	strcpy(keyCodes[num],"underscore"); keySyms[num++] = XK_underscore;
	strcpy(keyCodes[num],"parenleft"); keySyms[num++] = XK_parenleft;
	strcpy(keyCodes[num],"parenright"); keySyms[num++] = XK_parenright;
	strcpy(keyCodes[num],"braceleft"); keySyms[num++] = XK_braceleft;
	strcpy(keyCodes[num],"braceright"); keySyms[num++] = XK_braceright;
	strcpy(keyCodes[num],"bar"); keySyms[num++] = XK_bar;
	strcpy(keyCodes[num],"asciitilde"); keySyms[num++] = XK_asciitilde;
	strcpy(keyCodes[num],"exclam"); keySyms[num++] = XK_exclam;
	strcpy(keyCodes[num],"exclamdown"); keySyms[num++] = XK_exclamdown;
	strcpy(keyCodes[num],"brokenbar"); keySyms[num++] = XK_brokenbar;
#endif
#ifdef HAVE_WIN32
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"enter"); keySyms[num] = VK_RETURN;  b[num++] = 0x9C;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"backspace"); keySyms[num] = VK_BACK; b[num++] = 0x8E;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"tab"); keySyms[num] = VK_TAB; b[num++] = 0x8F;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"space"); keySyms[num] = VK_SPACE; b[num++] = 0xB9;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"escape"); keySyms[num] = VK_ESCAPE; b[num++] = 0x81;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"up"); keySyms[num] = VK_UP; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"down"); keySyms[num] = VK_DOWN; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"left"); keySyms[num] = VK_LEFT; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"right"); keySyms[num] = VK_RIGHT; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"alt"); keySyms[num] = VK_MENU; b[num++] = 0xB8;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"shift"); keySyms[num] = VK_LSHIFT; b[num++] = 0xAA;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"cntrl"); keySyms[num] = VK_LCONTROL; b[num++] = 0x9D;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"win"); keySyms[num] = VK_LWIN; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F1"); keySyms[num] = VK_F1; b[num++] = 0xBB;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F2"); keySyms[num] = VK_F2; b[num++] = 0xBC;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F3"); keySyms[num] = VK_F3; b[num++] = 0xBD;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F4"); keySyms[num] = VK_F4; b[num++] = 0xBE;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F5"); keySyms[num] = VK_F5; b[num++] = 0xBF;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F6"); keySyms[num] = VK_F6; b[num++] = 0xC0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F7"); keySyms[num] = VK_F7; b[num++] = 0xC1;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F8"); keySyms[num] = VK_F8; b[num++] = 0xC2;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F9"); keySyms[num] = VK_F9; b[num++] = 0xC3;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F10"); keySyms[num] = VK_F10; b[num++] = 0xC4;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F11"); keySyms[num] = VK_F11; b[num++] = 0xC5;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"F12"); keySyms[num] = VK_F12; b[num++] = 0xC6;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"end"); keySyms[num] = VK_END; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"home"); keySyms[num] = VK_HOME; b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"a"); keySyms[num] = VkKeyScan('a'); b[num++] = 0x9E;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"b"); keySyms[num] = VkKeyScan('b'); b[num++] = 0xB0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"c"); keySyms[num] = VkKeyScan('c'); b[num++] = 0xAE;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"d"); keySyms[num] = VkKeyScan('d'); b[num++] = 0xA0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"e"); keySyms[num] = VkKeyScan('e'); b[num++] = 0x92;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"f"); keySyms[num] = VkKeyScan('f'); b[num++] = 0xA1;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"g"); keySyms[num] = VkKeyScan('g'); b[num++] = 0xA2;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"h"); keySyms[num] = VkKeyScan('h'); b[num++] = 0xA3;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"i"); keySyms[num] = VkKeyScan('i'); b[num++] = 0x97;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"j"); keySyms[num] = VkKeyScan('j'); b[num++] = 0xA4;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"k"); keySyms[num] = VkKeyScan('k'); b[num++] = 0xA5;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"l"); keySyms[num] = VkKeyScan('l'); b[num++] = 0xA6;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"m"); keySyms[num] = VkKeyScan('m'); b[num++] = 0xB2;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"n"); keySyms[num] = VkKeyScan('n'); b[num++] = 0xB1;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"o"); keySyms[num] = VkKeyScan('o'); b[num++] = 0x98;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"p"); keySyms[num] = VkKeyScan('p'); b[num++] = 0x99;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"q"); keySyms[num] = VkKeyScan('q'); b[num++] = 0x90;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"r"); keySyms[num] = VkKeyScan('r'); b[num++] = 0x93;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"s"); keySyms[num] = VkKeyScan('s'); b[num++] = 0x9F;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"t"); keySyms[num] = VkKeyScan('t'); b[num++] = 0x94;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"u"); keySyms[num] = VkKeyScan('u'); b[num++] = 0x96;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"v"); keySyms[num] = VkKeyScan('v'); b[num++] = 0xAF;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"w"); keySyms[num] = VkKeyScan('w'); b[num++] = 0x91;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"x"); keySyms[num] = VkKeyScan('x'); b[num++] = 0xAD;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"y"); keySyms[num] = VkKeyScan('y'); b[num++] = 0x95;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"z"); keySyms[num] = VkKeyScan('z'); b[num++] = 0xAC;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"1"); keySyms[num] = VkKeyScan('1'); b[num++] = 0x82;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"2"); keySyms[num] = VkKeyScan('2'); b[num++] = 0x83;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"3"); keySyms[num] = VkKeyScan('3'); b[num++] = 0x84;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"4"); keySyms[num] = VkKeyScan('4'); b[num++] = 0x85;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"5"); keySyms[num] = VkKeyScan('5'); b[num++] = 0x86;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"6"); keySyms[num] = VkKeyScan('6'); b[num++] = 0x00;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"7"); keySyms[num] = VkKeyScan('7'); b[num++] = 0x88;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"8"); keySyms[num] = VkKeyScan('8'); b[num++] = 0x89;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"9"); keySyms[num] = VkKeyScan('9'); b[num++] = 0x8A;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"0"); keySyms[num] = VkKeyScan('0'); b[num++] = 0x8B;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"-"); keySyms[num] = VkKeyScan('-'); b[num++] = 0x8C;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"minus"); keySyms[num] = VkKeyScan('-'); b[num++] = 0x8C;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"="); keySyms[num] = VkKeyScan('='); b[num++] = 0x8D;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"["); keySyms[num] = VkKeyScan('['); b[num++] = 0x9A;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"]"); keySyms[num] = VkKeyScan(']'); b[num++] = 0x9B;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,";"); keySyms[num] = VkKeyScan(';'); b[num++] = 0xA7;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"semicolon"); keySyms[num] = VkKeyScan(';'); b[num++] = 0xA7;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"'"); keySyms[num] = VkKeyScan('\''); b[num++] = 0xA8;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"apostrophe"); keySyms[num] = VkKeyScan('\''); b[num++] = 0xA8;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"`"); keySyms[num] = VkKeyScan('`'); b[num++] = 0xA9;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"grave"); keySyms[num] = VkKeyScan('`'); b[num++] = 0xA9;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"\\"); keySyms[num] = VkKeyScan('\\'); b[num++] = 0xAB;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"backslash"); keySyms[num] = VkKeyScan('\\'); b[num++] = 0xAB;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,","); keySyms[num] = VkKeyScan(','); b[num++] = 0xB3;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"comma"); keySyms[num] = VkKeyScan(','); b[num++] = 0xB3;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"."); keySyms[num] = VkKeyScan('.'); b[num++] = 0xB4;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"period"); keySyms[num] = VkKeyScan('.'); b[num++] = 0xB4;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"/"); keySyms[num] = VkKeyScan('/'); b[num++] = 0xB5;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"slash"); keySyms[num] = VkKeyScan('/'); b[num++] = 0xB5;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"pageup"); keySyms[num] = VK_PRIOR; b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"pagedown"); keySyms[num] = VK_NEXT; b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"del"); keySyms[num] = VK_DELETE; b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"numbersign"); keySyms[num] = VkKeyScan('#'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"dollar"); keySyms[num] = VkKeyScan('$'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"percent"); keySyms[num] = VkKeyScan('%'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"ampersand"); keySyms[num] = VkKeyScan('&'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"asterisk"); keySyms[num] = VkKeyScan('*'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"plus"); keySyms[num] = VkKeyScan('+'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"colon"); keySyms[num] = VkKeyScan(':'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"less"); keySyms[num] = VkKeyScan('<'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"greater"); keySyms[num] = VkKeyScan('>'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"question"); keySyms[num] = VkKeyScan('?'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"at"); keySyms[num] = VkKeyScan('@'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"bracketleft"); keySyms[num] = VkKeyScan('['); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"bracketright"); keySyms[num] = VkKeyScan(']'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"braceleft"); keySyms[num] = VkKeyScan('{'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"braceright"); keySyms[num] = VkKeyScan('}'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"asciicircum"); keySyms[num] = VkKeyScan('^'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"underscore"); keySyms[num] = VkKeyScan('_'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"parenleft"); keySyms[num] = VkKeyScan('('); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"parenright"); keySyms[num] = VkKeyScan(')'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"brokenbar"); keySyms[num] = VkKeyScan('|'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"bar"); keySyms[num] = VkKeyScan('|'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"asciitilde"); keySyms[num] = VkKeyScan('~'); b[num++] = 0x0;
	strcpy_s(keyCodes[num],MAX_KEYCODE_LEN,"exclam"); keySyms[num] = VkKeyScan('!'); b[num++] = 0x0;
#endif
#ifdef HAVE_MACOSX
	strcpy(keyCodes[num],"enter"); keySyms[num++] = kVK_Return;
	strcpy(keyCodes[num],"backspace"); keySyms[num++] = kVK_Delete;
	strcpy(keyCodes[num],"tab"); keySyms[num++] = kVK_Tab;
	strcpy(keyCodes[num],"space"); keySyms[num++] = kVK_Space;
	strcpy(keyCodes[num],"escape"); keySyms[num++] = kVK_Escape;
	strcpy(keyCodes[num],"up"); keySyms[num++] = kVK_UpArrow;
	strcpy(keyCodes[num],"down"); keySyms[num++] = kVK_DownArrow;
	strcpy(keyCodes[num],"left"); keySyms[num++] = kVK_LeftArrow;
	strcpy(keyCodes[num],"right"); keySyms[num++] = kVK_RightArrow;
	strcpy(keyCodes[num],"alt"); keySyms[num++] = kVK_RightOption;
	strcpy(keyCodes[num],"shift"); keySyms[num++] = kVK_RightShift;
	strcpy(keyCodes[num],"cntrl"); keySyms[num++] = kVK_RightControl;
	strcpy(keyCodes[num],"F1"); keySyms[num++] = kVK_F1;
	strcpy(keyCodes[num],"F2"); keySyms[num++] = kVK_F2;
	strcpy(keyCodes[num],"F3"); keySyms[num++] = kVK_F3;
	strcpy(keyCodes[num],"F4"); keySyms[num++] = kVK_F4;
	strcpy(keyCodes[num],"F5"); keySyms[num++] = kVK_F5;
	strcpy(keyCodes[num],"F6"); keySyms[num++] = kVK_F6;
	strcpy(keyCodes[num],"F7"); keySyms[num++] = kVK_F7;
	strcpy(keyCodes[num],"F8"); keySyms[num++] = kVK_F8;
	strcpy(keyCodes[num],"F9"); keySyms[num++] = kVK_F9;
	strcpy(keyCodes[num],"F10"); keySyms[num++] = kVK_F10;
	strcpy(keyCodes[num],"F11"); keySyms[num++] = kVK_F11;
	strcpy(keyCodes[num],"F12"); keySyms[num++] = kVK_F12;
	strcpy(keyCodes[num],"end"); keySyms[num++] = kVK_End;
	strcpy(keyCodes[num],"home"); keySyms[num++] = kVK_Home;
	strcpy(keyCodes[num],"a"); keySyms[num++] = keyCodeForChar('a');
	strcpy(keyCodes[num],"b"); keySyms[num++] = keyCodeForChar('b');
	strcpy(keyCodes[num],"c"); keySyms[num++] = keyCodeForChar('c');
	strcpy(keyCodes[num],"d"); keySyms[num++] = keyCodeForChar('d');
	strcpy(keyCodes[num],"e"); keySyms[num++] = keyCodeForChar('e');
	strcpy(keyCodes[num],"f"); keySyms[num++] = keyCodeForChar('f');
	strcpy(keyCodes[num],"g"); keySyms[num++] = keyCodeForChar('g');
	strcpy(keyCodes[num],"h"); keySyms[num++] = keyCodeForChar('h');
	strcpy(keyCodes[num],"i"); keySyms[num++] = keyCodeForChar('i');
	strcpy(keyCodes[num],"j"); keySyms[num++] = keyCodeForChar('j');
	strcpy(keyCodes[num],"k"); keySyms[num++] = keyCodeForChar('k');
	strcpy(keyCodes[num],"l"); keySyms[num++] = keyCodeForChar('l');
	strcpy(keyCodes[num],"m"); keySyms[num++] = keyCodeForChar('m');
	strcpy(keyCodes[num],"n"); keySyms[num++] = keyCodeForChar('n');
	strcpy(keyCodes[num],"o"); keySyms[num++] = keyCodeForChar('o');
	strcpy(keyCodes[num],"p"); keySyms[num++] = keyCodeForChar('p');
	strcpy(keyCodes[num],"q"); keySyms[num++] = keyCodeForChar('q');
	strcpy(keyCodes[num],"r"); keySyms[num++] = keyCodeForChar('r');
	strcpy(keyCodes[num],"s"); keySyms[num++] = keyCodeForChar('s');
	strcpy(keyCodes[num],"t"); keySyms[num++] = keyCodeForChar('t');
	strcpy(keyCodes[num],"u"); keySyms[num++] = keyCodeForChar('u');
	strcpy(keyCodes[num],"v"); keySyms[num++] = keyCodeForChar('v');
	strcpy(keyCodes[num],"w"); keySyms[num++] = keyCodeForChar('w');
	strcpy(keyCodes[num],"x"); keySyms[num++] = keyCodeForChar('x');
	strcpy(keyCodes[num],"y"); keySyms[num++] = keyCodeForChar('y');
	strcpy(keyCodes[num],"z"); keySyms[num++] = keyCodeForChar('z');
	strcpy(keyCodes[num],"1"); keySyms[num++] = keyCodeForChar('1');
	strcpy(keyCodes[num],"2"); keySyms[num++] = keyCodeForChar('2');
	strcpy(keyCodes[num],"3"); keySyms[num++] = keyCodeForChar('3');
	strcpy(keyCodes[num],"4"); keySyms[num++] = keyCodeForChar('4');
	strcpy(keyCodes[num],"5"); keySyms[num++] = keyCodeForChar('5');
	strcpy(keyCodes[num],"6"); keySyms[num++] = keyCodeForChar('6');
	strcpy(keyCodes[num],"7"); keySyms[num++] = keyCodeForChar('7');
	strcpy(keyCodes[num],"8"); keySyms[num++] = keyCodeForChar('8');
	strcpy(keyCodes[num],"9"); keySyms[num++] = keyCodeForChar('9');
	strcpy(keyCodes[num],"0"); keySyms[num++] = keyCodeForChar('0');
	strcpy(keyCodes[num],"-"); keySyms[num++] = keyCodeForChar('-');
	strcpy(keyCodes[num],"="); keySyms[num++] = keyCodeForChar('=');
	strcpy(keyCodes[num],"["); keySyms[num++] = keyCodeForChar('[');
	strcpy(keyCodes[num],"]"); keySyms[num++] = keyCodeForChar(']');
	strcpy(keyCodes[num],";"); keySyms[num++] = keyCodeForChar(';');
	strcpy(keyCodes[num],"'"); keySyms[num++] = keyCodeForChar('\'');
	strcpy(keyCodes[num],"`"); keySyms[num++] = keyCodeForChar('`');
	strcpy(keyCodes[num],"\\"); keySyms[num++] = keyCodeForChar('\\');
	strcpy(keyCodes[num],","); keySyms[num++] = keyCodeForChar(',');
	strcpy(keyCodes[num],"."); keySyms[num++] = keyCodeForChar('.');
	strcpy(keyCodes[num],"/"); keySyms[num++] = keyCodeForChar('/');
	strcpy(keyCodes[num],"del"); keySyms[num++] = kVK_ForwardDelete;
	strcpy(keyCodes[num],"pageup"); keySyms[num++] = kVK_PageUp;
	strcpy(keyCodes[num],"pagedown"); keySyms[num++] = kVK_PageDown;
#endif

	numKeySyms = num;

	return 1;
}

int prepare_new_connection(linkedcons_t linked) {
	conSequence++;
	linked->conId = conSequence;

	sprintf_s (linked->snapshotFilename,64,"/tmp/freemote_cap-%d.jpg",linked->conId);

#ifdef HAVE_LIBXTST
	linked->dest_display = NULL;
#endif
	linked->solirc = NULL;
	linked->lirc_thread = NULL;
	linked->btinb = NULL;
	linked->soinb = NULL;
	linked->mythSocketCon = NULL;
	linked->numChunks = 0;
	linked->numChunksAcross = 0;
	linked->inbConnected = 0;
#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
	linked->lastImages = NULL;
#endif
#endif
	linked->next = NULL;

	// Connect to lircd
	if ((controlMethod & LIRC) != 0) {
		socon_t solirc = connectToSocket(lircHost, lircPort);
		if (solirc == NULL) {
			printf("Warning: Can't connect to lirc daemon\n");
			linked->lircConnected = 0;
			linked->solirc = 0;
			linked->lirc_thread = NULL;
			return 0;
		} else {
			linked->lircConnected = 1;
			linked->solirc = solirc;

			linked->lirc_thread = thread_create(consumeLirc, linked, (char*) "clirc");
			thread_run(linked->lirc_thread);
		}
	}

	// Connect to Desktop (if necessary, depends on platform)
	if ((controlMethod & DESKTOP) != 0) {

#ifdef HAVE_LIBXTST
		if ((linked->dest_display = XOpenDisplay(xDisplay)) == NULL) {
			fprintf(stderr, "%s: cannot connect to X server\n", xDisplay);
			exit(-1);
		}

		linked->screen = DefaultScreen(linked->dest_display);
		linked->root = RootWindow(linked->dest_display,linked->screen);
		linked->vis = DefaultVisual(linked->dest_display,linked->screen);
		linked->cm = DefaultColormap(linked->dest_display,linked->screen);

        linked->displayWidth = DisplayWidth (linked->dest_display, linked->screen);
        linked->displayHeight = DisplayHeight (linked->dest_display, linked->screen);

        // How many chunks do we need to cover the entire desktop?
        int accross = linked->displayWidth / MAX_SCREENSHOT_CHUNK_WIDTH;
        int down = linked->displayHeight / MAX_SCREENSHOT_CHUNK_HEIGHT;
        if (linked->displayWidth % MAX_SCREENSHOT_CHUNK_WIDTH > 0) {
        	accross++;
        }
        if (linked->displayHeight % MAX_SCREENSHOT_CHUNK_HEIGHT > 0) {
        	down++;
        }

        linked->numChunks = accross*down;
        linked->numChunksAcross = accross;

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
        linked->lastImages = (Imlib_Image *) malloc(linked->numChunks * sizeof(Imlib_Image));
        int c;
        for (c=0;c<linked->numChunks;c++) {
        	linked->lastImages[c] = NULL;
        }
#endif
#endif

#endif
	}

	// Connect to database
	// TBD

	return 1;
}

void mdnsd_run(void *arg)
{
    char hostName[128];
    char portNum[8];
    if (proxyType == CT_SOCKET) {
        gethostname(hostName,128);
	sprintf (portNum,"%d",acceptPort);
        announce (hostName,portNum,NULL);
    }
}

// For mac, we invoke from main.m which already has a main defined

#ifdef HAVE_MACOSX
int run(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
	int on;
	int ret;
	btcon_t btcon;
	socon_t soinb;
	linkedcons_t new_con;
#ifndef HAVE_VSTUDIO
	struct sigaction act;
#endif

        pxthread_t mdnsThread;

	setState(FREEMOTED_STATUS_STARTING,NULL);

	inbs = -1;
	bound = 0;

	lock = lock_create();
	if (lock == NULL) {
		fprintf(stderr,"[thread] mutex init error\n");
		setState(FREEMOTED_STATUS_FAILED,NULL);
		return 0;
	}

	if (parseOptions(argc, argv)) {
		setState(FREEMOTED_STATUS_FAILED,NULL);
		return 0;
	}

	if (checkArgs()) {
		setState(FREEMOTED_STATUS_FAILED,NULL);
		return 0;
	}

#ifndef HAVE_VSTUDIO
	act.sa_handler=sigterm;
    sigfillset(&act.sa_mask);
    act.sa_flags=SA_RESTART;
    sigaction(SIGTERM,&act,NULL);
    sigaction(SIGINT,&act,NULL);
    sigaction(SIGPIPE,&act,NULL);
    sigaction(SIGQUIT,&act,NULL);
    sigaction(SIGHUP,&act,NULL);
#endif

	if (readConfig() == 0) {
		setState(FREEMOTED_STATUS_FAILED,(void*)FREEMOTED_FAILURE_READCONFIG);
		return 0;
	}

	printf("Freemote Remote Proxy Server v1.4 is ");
	if (proxyType == CT_SOCKET) {
		printf("listening on port %d...\n",acceptPort);
	} else {
		printf("listening on bluetooth channel %d\n...",btChannel);
	}

	// Print some useul information
	if (!quiet) {
		if (proxyType == CT_BLUETOOTH) {
#ifdef HAVE_LIBBLUETOOTH
			printf("Proxy type: Bluetooth\n");
			printf("   Bluetooth Channel=%d\n", btChannel);
#else
			printf("Bluetooth requested but there is no support compiled in the executable.\n");
			setState(FREEMOTED_STATUS_FAILED,NULL);
			return 0;
#endif
		} else {
			printf("Proxy type: Socket\n");
			printf("   Accept Port: %d\n", acceptPort);
		}
		printf("\n");

		if ((controlMethod & MYTHFE) != 0) {
			printf("Myth Frontend Socket control is configured\n");
			printf("   Myth Host=%s:%d\n", mythHost, mythPort);
		} else {
			printf("Myth Frontend Socket control NOT configured\n");
			printf("   WARNING: JUMP buttons will not work\n");
		}
		printf("\n");
		if ((controlMethod & MYTHDB) != 0) {
			printf("Myth Database access will be used\n");
			printf("   Mysql host:%s\n", mysqlHost);
			printf("   Mysql port:%d\n", mysqlPort);
			printf("   Mysql user:%s\n", mysqlUser);
			if ((controlMethod & MYTHFE) != 0) {
				printf("   INFO: Database will be preferred over socket for QUERY functions\n");
			}
		} else {
			printf("Myth Database access NOT configured\n");
			if ((controlMethod & MYTHFE) == 0) {
				printf("   WARNING: QUERY functions will not be available\n");
			}
		}
		printf("\n");

		if ((controlMethod & LIRC) != 0) {
			printf("Lirc simulated events configured\n");
			printf("   Lirc host is %s:%d\n", lircHost, lircPort);
		} else {
			printf("Lirc simulated events NOT configured\n");
		}
		printf("\n");


		if ((controlMethod & DESKTOP) != 0) {
			printf("Desktop simulated mouse/keyboard events is configured\n");
#ifdef HAVE_LIBXTST
			printf("   XWin display is %s\n", xDisplay);
#endif
		} else {
			printf("Desktop simulated mouse/keyboard events NOT configured\n");
		}
		printf("\n");

	}

	// Allocate socket for incoming connections (either BLUETOOTH or SOCKET)
	if (proxyType == CT_BLUETOOTH) {
		inbs = 0;
#ifdef HAVE_LIBBLUETOOTH
		inbs = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
#endif
#ifdef HAVE_VSTUDIO
		inbs = socket (AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
#endif
		if (inbs <= 0) {
			printf ("Could not create bluetooth socket\n");
			setState(FREEMOTED_STATUS_FAILED,NULL);
			return 0;
		}
	} else if (proxyType == CT_SOCKET) {
		inbs = socket(AF_INET, SOCK_STREAM, 0);
		on = 1;

		ret = setsockopt(inbs, SOL_SOCKET, SO_REUSEADDR, (char*) &on, sizeof(on));

		if (ret != 0) {
			printf("Warning: could not set SO_REUSEADDR on socket");
		}

	} else {
		printf ("Unknown proxy type specified\n");
		setState(FREEMOTED_STATUS_FAILED,NULL);
		return 0;
	}

	setState(FREEMOTED_STATUS_LISTENING,NULL);

        if (proxyType == CT_SOCKET) {
            mdnsThread = thread_create(mdnsd_run,NULL,(char*) "mdns");
	    thread_run(mdnsThread);
        }

	// Main loop
	while (inbs != 0) {
		btcon = NULL;
		soinb = NULL;

		if (proxyType == CT_BLUETOOTH) {
			// Wait for a connection to come in from bluetooth
			btcon = waitForBluetoothConnection();
			if (btcon == NULL) {
				if (terminating == 0) {
					fprintf(stderr, "Could not accept bluetooth connection\n");
				}
				break;
			}
		} else {
			soinb = waitForSocketConnection();
			if (soinb == NULL) {
				if (terminating == 0) {
					fprintf(stderr, "Could not accept socket connection\n");
				}
				break;
			}
		}

		// New connection is inserted at head of list of cons
		new_con = (linkedcons_t) malloc(sizeof(linkedcons));
		prepare_new_connection(new_con);

		if (allCons != NULL) {
			new_con->next = allCons;
		}
		allCons = new_con;

		// Assign the new connection
		if (proxyType == CT_BLUETOOTH) {
			new_con->btinb = btcon;
		} else {
			new_con->soinb = soinb;
		}
		new_con->inbConnected = 1;

		new_con->handler_thread = thread_create(handleConnection, new_con, (char*) "hcon");
		thread_run(new_con->handler_thread);
	}

	if (inbs != 0) {
		closesocket(inbs);
		inbs = 0;
	}

	lock_destroy(lock);

        if (proxyType == CT_SOCKET) {
            kill_responder();
	    thread_join(mdnsThread);
	}

	setState(FREEMOTED_STATUS_STOPPED,NULL);
	printf ("Exiting...\n\n");

	terminating = 0;

	stateCallback = NULL;

	bound = 0;

	return 0;
}

// For windows
void f_startup(void *func)
{
	char *argv[3];
	argv[0] = "freemote.exe";

	if (stateCallback == NULL) {
		stateCallback = (freemoted_state_proc) func;
		main(1,argv);
	}
}

void f_setAcceptPort(int port)
{
	acceptPort = port;
}

void f_setConfigFile(char* file)
{
#ifdef HAVE_VSTUDIO
	configFile = (LPWSTR) file;
#else
	configFile = file;
#endif
}

void f_setConnectionType(int type)
{
	proxyType = type;
}
