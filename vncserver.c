/*
droid vnc server - Android VNC server
Copyright (C) 2009 Jose Pereira <onaips@gmail.com>

Modified for AML TV Boxes by kszaq <kszaquitto@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "common.h"
#include "framebuffer.h"
#include <unistd.h>
#include "common.h"
#include "newinput.h"

#include <rfb/rfb.h>
#include <rfb/keysym.h>

#include <time.h>
#include <unistd.h>

#define CONCAT2(a,b) a##b
#define CONCAT2E(a,b) CONCAT2(a,b)
#define CONCAT3(a,b,c) a##b##c
#define CONCAT3E(a,b,c) CONCAT3(a,b,c)
#define PICTURE_TIMEOUT (1.0/25.0)

char VNC_PASSWORD[256] = "";
int VNC_PORT = 5900;

unsigned int *cmpbuf;
unsigned int *vncbuf;

static rfbScreenInfoPtr vncscr;

int ufile;
int mouse_last = 0;
int relative_mode;
int last_x = 0;
int last_y = 0;
int new_x;
int new_y;
int redraw;
uint32_t idle = 1;
uint32_t standby = 1;
int timerredraw = 0;

//reverse connection
char *rhost = NULL;
int rport = 5500;

void (*update_screen)(void) = NULL;

#define PIXEL_TO_VIRTUALPIXEL_FB(i,j) ((j + scrinfo.yoffset) * scrinfo.xres_virtual + i + scrinfo.xoffset)
#define PIXEL_TO_VIRTUALPIXEL(i,j) ((j * screenformat.width) + i)

#define OUT 32
#include "updateScreen.c"
#undef OUT

void setIdle(int i) {
	idle=i;
}

ClientGoneHookPtr clientGone(rfbClientPtr cl) {
	return 0;
}

rfbNewClientHookPtr clientHook(rfbClientPtr cl) {
	cl->clientGoneHook=(ClientGoneHookPtr)clientGone;
	
	return RFB_CLIENT_ACCEPT;
}

void doptr(int buttonMask, int x, int y, rfbClientPtr cl){
	struct input_event       event;

//	printf("mouse: 0x%x at %d,%d\n", buttonMask, x,y);

	if ((x > (last_x+30)) ||  (x < (last_x-30)) || (y > (last_y+30)) || (y < (last_y-30))) {
		new_x=x*2;
		new_y=y*2;
		redraw = 1;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, NULL);
		if (relative_mode) {
			event.type = EV_REL;
			event.code = REL_X;
			event.value = new_x - last_x;
		}
		else {
			event.type = EV_ABS;
			event.code = ABS_X;
			event.value = new_x;
		}
		write(ufile, &event, sizeof(event));

		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, NULL);
		if (relative_mode) {
			event.type = EV_REL;
			event.code = REL_Y;
			event.value = new_y - last_y;
		}
		else {
			event.type = EV_ABS;
			event.code = ABS_Y;
			event.value = new_y;
		}
		write(ufile, &event, sizeof(event));

		last_x = new_x;
		last_y = new_y;

		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, NULL);
		event.type = EV_SYN;
		event.code = SYN_REPORT; 
		event.value = 0;
		write(ufile, &event, sizeof(event));
		if (mouse_last != buttonMask) {
			int left_l = mouse_last & 0x1;
			int left_w = buttonMask & 0x1;

			if (left_l != left_w) {
				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_KEY;
				event.code = BTN_LEFT;
				event.value = left_w;
				write(ufile, &event, sizeof(event));

				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_SYN;
				event.code = SYN_REPORT; 
				event.value = 0;
				write(ufile, &event, sizeof(event));
			}

			int middle_l = mouse_last & 0x2;
			int middle_w = buttonMask & 0x2;

			if (middle_l != middle_w) {
				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_KEY;
				event.code = BTN_MIDDLE;
				event.value = middle_w >> 1;
				write(ufile, &event, sizeof(event));

				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_SYN;
				event.code = SYN_REPORT; 
				event.value = 0;
				write(ufile, &event, sizeof(event));
			}
			int right_l = mouse_last & 0x4;
			int right_w = buttonMask & 0x4;

			if (right_l != right_w) {
				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_KEY;
				event.code = BTN_RIGHT;
				event.value = right_w >> 2;
				write(ufile, &event, sizeof(event));

				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, NULL);
				event.type = EV_SYN;
				event.code = SYN_REPORT; 
				event.value = 0;
				write(ufile, &event, sizeof(event));
			}
			mouse_last = buttonMask;
		}	
	}
}


void initVncServer(int argc, char **argv) {
	vncbuf = calloc(screenformat.width * screenformat.height, screenformat.bitsPerPixel/CHAR_BIT);
	cmpbuf = calloc(screenformat.width * screenformat.height, screenformat.bitsPerPixel/CHAR_BIT);
	
	assert(vncbuf != NULL);
	assert(cmpbuf != NULL);
	
	vncscr = rfbGetScreen(&argc, argv, screenformat.width, screenformat.height, 0 /* not used */ , 3,  screenformat.bitsPerPixel/CHAR_BIT);
	
	assert(vncscr != NULL);
	
	vncscr->desktopName = "OSMC Vero 4K";
	vncscr->frameBuffer =(char *)vncbuf;
	vncscr->port = VNC_PORT;
	vncscr->kbdAddEvent = dokey;
	vncscr->ptrAddEvent = doptr;
	vncscr->newClientHook = (rfbNewClientHookPtr)clientHook;
	
	if (strcmp(VNC_PASSWORD, "") != 0) {
		char **passwords = (char **)malloc(2 * sizeof(char **));
		passwords[0] = VNC_PASSWORD;
		passwords[1] = NULL;
		vncscr->authPasswdData = passwords;
		vncscr->passwordCheck = rfbCheckPasswordByList;
	}
	
	vncscr->serverFormat.redShift = screenformat.redShift;
	vncscr->serverFormat.greenShift = screenformat.greenShift;
	vncscr->serverFormat.blueShift = screenformat.blueShift;
	
	vncscr->serverFormat.redMax = (( 1 << screenformat.redMax) -1);
	vncscr->serverFormat.greenMax = (( 1 << screenformat.greenMax) -1);
	vncscr->serverFormat.blueMax = (( 1 << screenformat.blueMax) -1);
	
	vncscr->serverFormat.trueColour = TRUE;
	vncscr->serverFormat.bitsPerPixel = screenformat.bitsPerPixel;
	vncscr->alwaysShared = TRUE;
	
	rfbInitServer(vncscr);
	
	update_screen = update_screen_32;
	
	/* Mark as dirty since we haven't sent any updates at all yet. */
	rfbMarkRectAsModified(vncscr, 0, 0, vncscr->width, vncscr->height);
}

void close_app() {
	L("Cleaning up...\n");
	closeFB();
	closeUinput();
	exit(0); /* normal exit status */
}

void extractReverseHostPort(char *str) {
	int len = strlen(str);
	char *p;
	/* copy in to host */
	rhost = (char *) malloc(len + 1);
	if (! rhost) {
		L("reverse_connect: could not malloc string %d\n", len);
		exit(-1);
	}
	strncpy(rhost, str, len);
	rhost[len] = '\0';
	
	/* extract port, if any */
	if ((p = strrchr(rhost, ':')) != NULL) {
		rport = atoi(p + 1);
		if (rport < 0) {
			rport = -rport;
		}
		else if (rport < 20) {
			rport = 5500 + rport;
		}
		*p = '\0';
	}
}

void printUsage(char **argv) {
	L("\naml-server [parameters]\n"
		"-f <device>\t- Framebuffer device\n"
		"-h\t\t- Print this help\n"
		"-p <password>\t- Password to access server\n"
		"-R <host:port>\t- Host for reverse connection\n");
}

int main(int argc, char **argv) {
	long usec;
	
	if(argc > 1) {
		int i = 1;
		while(i < argc) {
		if(*argv[i] == '-') {
			switch(*(argv[i] + 1)) {
				case 'h':
					printUsage(argv);
					exit(0);
					break;
				case 'p':
					i++;
					strcpy(VNC_PASSWORD,argv[i]);
					break;
				case 'f':
					i++;
					FB_setDevice(argv[i]);
					break;
				case 'P':
					i++;
					VNC_PORT=atoi(argv[i]);
					break;
				case 'R':
					i++;
					extractReverseHostPort(argv[i]);
					break;
			}
		}
		i++;
		}
	}
	
	L("Initializing grabber method...\n");
	initFB();
	
	L("Initializing virtual keyboard...\n");
	initUinput();
	
	L("Initializing VNC server:\n");
	L("	width:  %d\n", (int)screenformat.width);
	L("	height: %d\n", (int)screenformat.height);
	L("	bpp:    %d\n", (int)screenformat.bitsPerPixel);
	L("	port:   %d\n", (int)VNC_PORT);
	
	L("Colourmap_rgba=%d:%d:%d    length=%d:%d:%d\n", screenformat.redShift, screenformat.greenShift, screenformat.blueShift,
		screenformat.redMax, screenformat.greenMax, screenformat.blueMax);
	
	initVncServer(argc, argv);
	
	if (rhost) {
		rfbClientPtr cl;
		cl = rfbReverseConnection(vncscr, rhost, rport);
		if (cl == NULL) {
			char *str = malloc(255 * sizeof(char));
			L("Couldn't connect to remote host: %s\n",rhost);
			free(str);
		}
		else {
			cl->onHold = FALSE;
			rfbStartOnHoldClient(cl);
		}
	}
	
	
	while (1) {
		usec = (vncscr->deferUpdateTime + standby) * 500;
		rfbProcessEvents(vncscr, usec);
		if (idle)
			standby = 100;
		else
			standby = 50;
		if (vncscr->clientHead != NULL){
			if (redraw != 0){
				update_screen(); 
				redraw = 0;
			}
			else {
				if ((timerredraw = 15)){
					update_screen();
					timerredraw = 0;
				}
				else {
					timerredraw = timerredraw +1;
				}
			}
		}
	}
	close_app();
}
