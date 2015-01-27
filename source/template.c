#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <asndlib.h>
#include <aesndlib.h>
#include "oggplayer.h"

#include "sound_pcm.h"
#include "bg_music_ogg.h"

#define NUM_PARTICLES 10
#define PARTICLE_MAX_WIDTH 20
#define PARTICLE_MAX_HEIGHT 20
#define PARTICLE_MIN_WIDTH 2
#define PARTICLE_MIN_HEIGHT 2
#define PARTICLE_MAX_SPEED 10
#define PARTICLE_MIN_SPEED 1

const int PARTICLE_MAX_FREQ  = VOICE_FREQ48KHZ;
const int PARTICLE_MIN_FREQ = VOICE_FREQ48KHZ>>1;
const int PARTICLE_MAX_SIZE = PARTICLE_MAX_WIDTH*PARTICLE_MAX_HEIGHT;
const int PARTICLE_MIN_SIZE = PARTICLE_MIN_WIDTH*PARTICLE_MIN_HEIGHT;

// GLOBAL VARS
static void *xfb = NULL;
static GXRModeObj *vmode = NULL;
int g_fb_height, g_fb_width;
ir_t ir;
s32 voice;

typedef struct {
	int pos_x, pos_y;   // screen coordinates
	int size_x, size_y; // dimensions
	int dx, dy;        // speed values
	int freq;		   // freequency of collision sound
}Particle;

Particle g_particles[NUM_PARTICLES];

/*****************************************************************************
 * Structure of the external framebuffer xfb[]:                              *
 *                                                                           *
 *		0			320			640                                          *
 *    0 -------------------------> x                                         *
 * 		|xxxxxxxxxxxx|                                                       *
 * 		|xxxxxxxxxxxx|                                                       *
 * 		|xxxxxxxxxxxx|                                                       *
 * 	240	|xxxxxxxxxxxx|(319,239)                                              *
 * 		|xxxxxxxxxxxx|                                                       *
 * 		|xxxxxxxxxxxx|                                                       *
 * 		|xxxxxxxxxxxx|                                                       *
 * 	480	|------------|                                                       *
 * 		y                                                                    *
 * 		                                                                     *
 * 		xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode)) yields a 1D data  *
 * 		structure of size (vmode->fb_width/2) * vmode->xfb_height storing    *
 * 		u32-valued data in row major order with half resolution in x-dir.    *
 * 		                                                                     *
 * 		The linearized index L of a 2D point                                 *
 * 		p=(x,y) € [0..fb_width-1] x [0..fb_height-1] then reads as follows:  *
 * 		                                                                     *
 * 		L(x,y) = ((vmode->fb_width) * y + x)/VI_DISPLAY_PIX_SZ,              *
 * 		                                                                     *
 * 		with VI_DISPLAY_PIX_SZ = 2 and width stride (vmode->fb_width/2).     *
 *                                                                           *
 * 		Example:                                                             *
 *                                                                           *
 * 		vmode->fb_width = 640, vmode->xfb_height = 480,                      *
 * 		p = (x,y) = (639,479)                                                *
 * 		=> L(639,479) = 320 * 479 + (639>>1) = 153599.                       *
 *****************************************************************************/

void drawHLine0 (int x1, int x2, int y, int color) {
    int i;
    y = 320 * y;
    x1 >>= 1;
    x2 >>= 1;
    for (i = x1; i <= x2; i++) {
        u32 *tmpfb = xfb;
        tmpfb[y+i] = color;
    }
}

void drawHLine (int x1, int x2, int y, int color) {
    int i;
    x1 >>= 1;
    x2 >>= 1;
    y *= g_fb_width>>1;
    u32 *tmpfb = xfb;
    tmpfb += y;
    for (i = x1; i <= x2; i++) {
        tmpfb[i] = color;
    }
}

void drawVLine0 (int x, int y1, int y2, int color) {
    int i;
    x >>= 1;
    for (i = y1; i <= y2; i++) {
        u32 *tmpfb = xfb;
        tmpfb[x + ((640 * i) >> 1)] = color;
    }
}

void drawVLine (int x, int y1, int y2, int color) {
    int i;
    x >>= 1;
    u32 *tmpfb = xfb;
    tmpfb += x;
    for (i = y1; i <= y2; i++) {
        tmpfb[(g_fb_width>>1)*i] = color;
    }
}

void drawBox (int x1, int y1, int x2, int y2, int color) {
    drawHLine (x1, x2, y1, color);
    drawHLine (x1, x2, y2, color);
    drawVLine (x1, y1, y2, color);
    drawVLine (x2, y1, y2, color);
}

void drawParticle(int x1, int y1, int x2, int y2, int color) {
	int i;
	for(i=y1; i<=y2; i++)
		drawHLine(x1, x2, i, color);
}

int rnd(int a, int b) {
	return rand() % (b-a+1) + a;
}

void initParticles() {
	srand(time(NULL));
	int i, size;
	for(i=0; i<NUM_PARTICLES; i++) {
		// Create particle with random size, position and speed
		g_particles[i].size_x = rnd(PARTICLE_MIN_WIDTH, PARTICLE_MAX_WIDTH);
		g_particles[i].size_y = rnd(PARTICLE_MIN_HEIGHT, PARTICLE_MAX_HEIGHT);
		g_particles[i].pos_x = rnd(0, g_fb_width-g_particles[i].size_x-1);
		g_particles[i].pos_y = rnd(0, g_fb_height-g_particles[i].size_y-1);
		g_particles[i].dx = rnd(PARTICLE_MIN_SPEED, PARTICLE_MAX_SPEED);
		g_particles[i].dy = rnd(PARTICLE_MIN_SPEED, PARTICLE_MAX_SPEED);

		if(rand() % 1) g_particles[i].dx = -g_particles[i].dx;
		if(rand() % 1) g_particles[i].dy = -g_particles[i].dy;

		// Determine frequency of collision sound depending on particle size
		// (linear interpolation)
		size = g_particles[i].size_x*g_particles[i].size_y;
		g_particles[i].freq = PARTICLE_MIN_FREQ +
							 (PARTICLE_MAX_FREQ - PARTICLE_MIN_FREQ) /
							 (PARTICLE_MAX_SIZE - PARTICLE_MIN_SIZE) *
							 (PARTICLE_MAX_SIZE - size);
	}
}

void updateParticles() {
	int i;
	for(i=0; i<NUM_PARTICLES; i++) {
		// Apply changes in movement
		g_particles[i].pos_x += g_particles[i].dx;
		g_particles[i].pos_y += g_particles[i].dy;

		// Check for collisions with screen boundaries
		if(g_particles[i].pos_x < 0 || g_particles[i].pos_x > (g_fb_width-g_particles[i].size_x)) {
			g_particles[i].dx = -g_particles[i].dx;
			g_particles[i].pos_x = (g_particles[i].pos_x<0)?0:g_fb_width-g_particles[i].size_x;
			voice=ASND_GetFirstUnusedVoice();
			ASND_SetVoice(voice, VOICE_MONO_16BIT, g_particles[i].freq, 0,
						 (u8 *)sound_pcm, sound_pcm_size, 255, 255, NULL);
		}

		if(g_particles[i].pos_y < 0 || g_particles[i].pos_y > (g_fb_height-g_particles[i].size_y)) {
			g_particles[i].dy = -g_particles[i].dy;
			g_particles[i].pos_y = (g_particles[i].pos_y<0)?0:g_fb_height-g_particles[i].size_y;
			voice=ASND_GetFirstUnusedVoice();
			ASND_SetVoice(voice, VOICE_MONO_16BIT, g_particles[i].freq, 0,
						 (u8 *)sound_pcm, sound_pcm_size, 255, 255, NULL);
		}

		// Display updated particle
		drawParticle(g_particles[i].pos_x,  g_particles[i].pos_y,
					 g_particles[i].pos_x + g_particles[i].size_x - 1,
					 g_particles[i].pos_y + g_particles[i].size_y - 1,
					 COLOR_WHITE);
	}
}

void printInfo() {

	// The console understands VT terminal escape codes
	// This positions the cursor on row 2, column 1
	// we can use variables for this with format codes too
	// e.g. printf ("\x1b[%d;%dH", row, column );
	printf("\x1b[2;1H");

	printf("Hello World!\n\n");

	printf("\taa:        %d\n", vmode->aa);
	printf("\tfbWidtht:  %d\n", vmode->fbWidth);
	printf("\tefbHeight: %d\n", vmode->efbHeight);
	printf("\txfbHeight: %d\n", vmode->xfbHeight);
	printf("\txfbMode:   %d\n", vmode->xfbMode);
	printf("\tviWidth:   %d\n", vmode->viWidth);
	printf("\tviHeight:  %d\n", vmode->viHeight);
	printf("\tviTVMode:  %d\n", vmode->viTVMode);
	printf("\tviXOrigin: %d\n", vmode->viXOrigin);
	printf("\tviYOrigin: %d\n", vmode->viYOrigin);
}

void init() {

	/*************************************************************************
	 * VIDEO                                                                 *
	 *************************************************************************/
	// Initialise the video system
	VIDEO_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	vmode = VIDEO_GetPreferredMode(NULL);

	// widescreen fix
	if(CONF_GetAspectRatio() == CONF_ASPECT_16_9)
		vmode->viWidth = VI_MAX_WIDTH_PAL;

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	g_fb_width = vmode->fbWidth;
	g_fb_height = vmode->xfbHeight;

	// Initialise the console, required for printf
	console_init(xfb,20,20,g_fb_width,g_fb_height,g_fb_width*VI_DISPLAY_PIX_SZ);

	// Set up the video registers with the chosen mode
	VIDEO_Configure(vmode);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	/*************************************************************************
	 * AUDIO                                                                 *
	 *************************************************************************/
	// Initialise the audio subsysten
	AUDIO_Init(NULL);
	ASND_Init(NULL);
	ASND_Pause(0);
	//PlayOgg(bg_music_ogg, bg_music_ogg_size, 0, OGG_ONE_TIME);

	/*************************************************************************
	 * CONTROLS                                                              *
	 *************************************************************************/
	// This function initialises the attached controllers
	WPAD_Init();

	//	Configure infrared system of the Wii remote
	WPAD_SetVRes(WPAD_CHAN_0, g_fb_width, g_fb_height);
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
	WPAD_IR(0, &ir);

	/*************************************************************************
	 * GAME RELATED STUFF                                                    *
	 *************************************************************************/
	// Setup particle system
	initParticles();

}

/******************************************************************************
 * Main method
 *****************************************************************************/
int main(int argc, char **argv) {

	init();

	while(1) {
//		// Call WPAD_ScanPads each loop, this reads the latest controller states
//		WPAD_ScanPads();
//
//		// WPAD_ButtonsDown tells us which buttons were pressed in this loop
//		// this is a "one shot" state which will not fire again until the button
//		// has been released
//		u32 pressed = WPAD_ButtonsDown(WPAD_CHAN_0);
//
//		// We return to the launcher application via exit
//		if ( pressed & WPAD_BUTTON_HOME ) exit(0);
//
//		if( pressed & WPAD_BUTTON_A ) {
//			printf("Button A pressed.\n");
//		}
//
//		u32 buttonsHeld = WPAD_ButtonsHeld(WPAD_CHAN_0);
//
//		if (buttonsHeld & WPAD_BUTTON_A ) {
//			printf("Button A is being held down.\n");
//		}
//
//		u16 buttonsUp = WPAD_ButtonsUp(WPAD_CHAN_0);
//
//		if (buttonsUp & WPAD_BUTTON_A ) {
//			printf("Button A released.\n");
//		}
//
//		/*if (PAD_StickY(WPAD_CHAN_0) > 18) {
//			printf("Joystick moved up.\n");
//		}
//
//		if (PAD_StickY(WPAD_CHAN_0) < -18) {
//			printf("Joystick moved down.\n");
//		}*/
//
		VIDEO_ClearFrameBuffer(vmode, xfb, COLOR_BLACK);

		// Console output
		//printInfo();

		// Draw stuff
		drawBox((g_fb_width>>1)-10, (g_fb_height>>1)-10,
				(g_fb_width>>1)+10, (g_fb_height>>1)+10, COLOR_WHITE);
		drawBox(1, 1, g_fb_width-1, g_fb_height-1, COLOR_WHITE);
		drawParticle(g_fb_width>>1, g_fb_height>>1,
				     g_fb_width>>1, g_fb_height>>1,
					 COLOR_WHITE);

		updateParticles();

		// Wait for the next frame
		VIDEO_WaitVSync();
	}

	return 0;
}
