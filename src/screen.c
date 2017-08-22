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

#ifdef HAVE_LIBGIBLIB
#ifdef HAVE_LIBIMLIB2
#include <giblib/giblib.h>
#include <math.h>

typedef struct _entry Entry;

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define ABS(a)	 (((a) < 0) ? -(a) : (a))

struct _entry {
	int x1,x2;
	int y1,y2;
	int softDelete;
};

// Compare two pixels between two images in the same location
// Returns 0 if they are 'close' enough, 1 otherwise
int compare(Imlib_Image lastImage, Imlib_Image nextImage,int x,int y) {
	Imlib_Color c1;
	Imlib_Color c2;

	imlib_context_set_image(lastImage);
	imlib_image_query_pixel(x,y,&c1);

	imlib_context_set_image(nextImage);
	imlib_image_query_pixel(x,y,&c2);

	int dist = sqrt((c1.red-c2.red)*(c1.red-c2.red)+(c1.blue-c2.blue)*(c1.blue-c2.blue)+(c1.green-c2.green)*(c1.green-c2.green));

	if (dist > 16) {
		return 1;
	} else {
		return 0;
	}
}

// Search the list of start/end difference ranges for something either completely
// enclosing the given start/end entry or one that is 'close' to it on both ends
// 'close' is defined by anything with an endpoint within XTHRESHOLD pixels
gib_list* findAbove(gib_list **yList, int y, Entry *e)
{
	int XTHRESHOLD = 32;

	gib_list *node = yList[y];
	while (node != NULL) {
		if (node->data != NULL) {
			Entry* aboveEntry = (Entry*)node->data;
			if (aboveEntry->softDelete == 0) {
				if (e->x1 >= aboveEntry->x1 && e->x2 <= aboveEntry->x2) {
					return node;
				}
				if (aboveEntry->x1 >= e->x1 && aboveEntry->x2 <= e->x2) {
					return node;
				}

				int dx1 = ABS(aboveEntry->x1 - e->x1);
				int dx2 = ABS(aboveEntry->x2 - e->x2);
				if (dx1 < XTHRESHOLD && dx2 < XTHRESHOLD) {
					return node;
				}
			}
		}
		node = node->next;
	}
	return NULL;
}

// Search through all range lists in all rows and remove entries that are already
// enclosed by other range entries.
void eliminateCovered(gib_list **yList, int height, gib_list *found)
{
	Entry *e2 = (Entry*)found->data;
	if (e2 == NULL || e2->softDelete == 1) { return; }

	int y;
	for (y=0;y<height;y++) {
		gib_list *node = yList[y];
		while (node != NULL) {
			if (node != found && node->data != NULL) {
				Entry *e = (Entry*)node->data;
				if (e->softDelete == 0) {
					if (e2->x1 >= e->x1 && 
					    e2->x2 <= e->x2 && 
					    e2->y1 >= e->y1 && 
					    e2->y2 <= e->y2)
					{
						e2->softDelete = 1;
						return;
					}
				}
			}
			node = node->next;
		}
	}
}

// Build a list of regions that have changed between lastImage and nextImage
// Up to maxRegions will be created.  If the maximum is hit, the delta regions returned are
// not guaranteed to build nextImage from lastImage in its entirety.
// Returns the percentage (as decimal between 0 and 1) of area that has changed between
// lastImage and nextImage given the deltas.
float findDeltas(int maxRegions, int width, int height, Imlib_Image lastImage, Imlib_Image nextImage, int *numRegions, Imlib_Image* im, int* imw, int* imh, int* top, int* left)
{
	int XTHRESHOLD = 32;

	Entry *e;
	int newEntry = 0;
	int numSame = 0;

	gib_list **yList = (gib_list**) malloc(sizeof(gib_list*)*height);

	//
	// Step 1: Build a list of start/end range entries in the x direction 1 pixel high for areas that
	// have changed.  Do this for each row of the before and after images.  After detecting a
	// difference to start a range, at least XTHRESHOLD consecutive different pixels must be
	// encountered before closing off the range.
	//
	int x,y;
	for (y=0;y<height;y++) {
		yList[y] = gib_list_new();
		for (x =0;x<width;x++) {
			if (newEntry == 1) {
				if (compare(lastImage, nextImage, x, y) == 0) {
					// We need to see at least XTHRESHOLD numSame before closing off this entry
					numSame++;
					if (numSame >= XTHRESHOLD) {
						// No longer building this entry
						newEntry = 0;
						e->x2 = x-XTHRESHOLD+1;
						gib_list_add_end(yList[y],e);
						//printf ("RAW %d,%d->%d,%d\n",e->x1,e->y1,e->x2,e->y2);
					}
				} else {
					numSame = 0;
				}
			} else {
				if (compare(lastImage, nextImage, x, y) == 1) {
					// Start new entry since we saw a difference
					newEntry = 1;
					numSame = 0;
					e = (Entry *) malloc(sizeof(Entry));
					e->x1 = x; e->y1 = y; e->y2 = y; e->softDelete = 0;
				}
			}
		}

		if (newEntry == 1) {
			// Fell off the end, close this entry
			newEntry = 0;
			e->x2 = width-1;
			gib_list_add_end(yList[y],e);
//			printf ("RAW %d,%d->%d,%d\n",e->x1,e->y1,e->x2,e->y2);
		}
	}

	//
	// Step 2: Consolidation Pass 1.  For rows 1->height-1 and for each entry on a row, look
	// through all the range entries directly above and merge ranges that are either enclosed by
	// this entry or 'close' enough to be expanded.  The 'close' enough entry above is soft deleted
	// and the current entry is expanded to include the one found above (by inheriting it's y value).
	//
	for (y=1;y<height;y++) {
		gib_list *node = yList[y];
		while (node != NULL) {
			if (node->data != NULL) {
				e = (Entry*)node->data;
				gib_list* above = NULL;
				above = findAbove(yList, y-1, e);
				if (above != NULL && above->data != NULL) {
					Entry* e2 = (Entry*) above->data;
					e2->softDelete = 1;

					e->x1 = MIN(e->x1,e2->x1);
					e->x2 = MAX(e->x2,e2->x2);
					e->y1 = e2->y1;
				}
			}
			node = node->next;
		}
	}

	//
	// Step 3: Consolidation Pass 3 : Some entries will remain that will be completely enclosed by
	// other ranges that have expanded.  These should be removed as they are redundant.
	//
	for (y=0;y<height;y++) {
		gib_list *node = yList[y];
		while (node != NULL) {
			if (node->data != NULL) {
				eliminateCovered(yList,height,node);
			}
			node = node->next;
		}
	}

	//
	// Step 4: Place all the found regions in the given return arrays.
	//
	int totalArea = 0;
	for (y=0;y<height && *numRegions < maxRegions;y++) {
		gib_list *node = yList[y];
		while (node != NULL && *numRegions < maxRegions) {
			if (node->data != NULL) {
				e = (Entry*)node->data;

				if (e->softDelete == 0) {
					imw[*numRegions] = e->x2-e->x1+1;
					imh[*numRegions] = e->y2-e->y1+1;
					top[*numRegions] = e->y1;
					left[*numRegions] = e->x1;
					im[*numRegions] = gib_imlib_create_cropped_scaled_image(nextImage,left[*numRegions],top[*numRegions],imw[*numRegions],imh[*numRegions],imw[*numRegions],imh[*numRegions],0);
					totalArea += imw[*numRegions]*imh[*numRegions];
					//printf ("SURVIVOR %d,%d->%d,%d\n",left[*numRegions],top[*numRegions],imw[*numRegions],imh[*numRegions]);

					*numRegions=*numRegions+1;
				}
				free(node->data);
			}
			node = node->next;
		}
		gib_list_free(yList[y]);
	}

	free(yList);

	float ratio = 0.0;
	if (*numRegions > 0) {
		ratio = (float)(totalArea)/(float)(width*height);
		//printf ("%f, ",ratio);
	} 
	return ratio;
}
#endif
#endif
