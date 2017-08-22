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


#ifndef SCREEN_H_
#define SCREEN_H_

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
#include <giblib/giblib.h>
#endif
#endif

#define MAX_SCREENSHOT_CHUNK_WIDTH 800
#define MAX_SCREENSHOT_CHUNK_HEIGHT 600

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
float findDeltas(int maxRegions, int width, int height, Imlib_Image lastImage, Imlib_Image nextImage, int *numRegions, Imlib_Image* im, int* imw, int* imh, int* top, int* left);
#endif
#endif

#endif /* SCREEN_H_ */
