#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <aesndlib.h>
#include <gcmodplay.h>

#include "sound_mod.h"

#define NUM_PARTICLES 1
#define PARTICLE_MAX_WIDTH 20
#define PARTICLE_MAX_HEIGHT 20
#define PARTICLE_MAX_SPEED 5
#define PARTICLE_MIN_SPEED 1

// GLOBAL DEFS
static void *xfb = NULL;
static GXRModeObj *rmode = NULL;
static MODPlay sound;
int g_fb_height, g_fb_width;
ir_t ir;

typedef struct {
	int pos_x, pos_y;   // screen coordinates
	int size_x, size_y; // dimensions
	int dx, dy;        // speed values
}Particle;

Particle g_particles[NUM_PARTICLES];

// PROTOTYPES
void init();
void DrawHLine(int, int, int, int);
void DrawVLine(int, int, int, int);
void DrawBox (int, int, int, int, int);
void DrawParticle(int x, int y, int width, int height, int color);
void initParticles();
void updateParticles();

/******************************************************************************
 * Main method
 *****************************************************************************/
int main(int argc, char **argv) {

	init();

	// The console understands VT terminal escape codes
	// This positions the cursor on row 2, column 1
	// we can use variables for this with format codes too
	// e.g. printf ("\x1b[%d;%dH", row, column );
	printf("\x1b[2;1H");

	printf("Hello World!\n\n");

	// Look at some interesting data
	printf("\taa:        %d\n", rmode->aa);
	printf("\tfbWidtht:  %d\n", rmode->fbWidth);
	printf("\tefbHeight: %d\n", rmode->efbHeight);
	printf("\txfbHeight: %d\n", rmode->xfbHeight);
	printf("\txfbMode:   %d\n", rmode->xfbMode);
	printf("\tviHeight:  %d\n", rmode->viHeight);
	printf("\tviWidth:   %d\n", rmode->viWidth);
	printf("\tviTVMode:  %d\n", rmode->viTVMode);
	printf("\tviXOrigin: %d\n", rmode->viXOrigin);
	printf("\tviYOrigin: %d\n", rmode->viYOrigin);

	while(1) {

		// Call WPAD_ScanPads each loop, this reads the latest controller states
		WPAD_ScanPads();

		// WPAD_ButtonsDown tells us which buttons were pressed in this loop
		// this is a "one shot" state which will not fire again until the button
		// has been released
		u32 pressed = WPAD_ButtonsDown(WPAD_CHAN_0);

		// We return to the launcher application via exit
		if ( pressed & WPAD_BUTTON_HOME ) exit(0);

		if( pressed & WPAD_BUTTON_A ) {
			printf("Button A pressed.\n");
		}

		u32 buttonsHeld = WPAD_ButtonsHeld(WPAD_CHAN_0);

		if (buttonsHeld & WPAD_BUTTON_A ) {
			printf("Button A is being held down.\n");
		}

		u16 buttonsUp = WPAD_ButtonsUp(WPAD_CHAN_0);

		if (buttonsUp & WPAD_BUTTON_A ) {
			printf("Button A released.\n");
		}

		/*if (PAD_StickY(WPAD_CHAN_0) > 18) {
			printf("Joystick moved up.\n");
		}

		if (PAD_StickY(WPAD_CHAN_0) < -18) {
			printf("Joystick moved down.\n");
		}*/

		VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

		DrawBox((g_fb_width>>1)-10, (g_fb_height>>1)-10,
				(g_fb_width>>1)+10, (g_fb_height>>1)+10, COLOR_WHITE);
		DrawBox(1, 1, g_fb_width-1, g_fb_height-1, COLOR_WHITE);
		DrawParticle(g_fb_width>>1, g_fb_height>>1, 1, 1, COLOR_WHITE);

		updateParticles();

		// Wait for the next frame
		VIDEO_WaitVSync();
	}

	return 0;
}

void init() {

	/*************************************************************************
	 * VIDEO                                                                 *
	 *************************************************************************/
	// Initialise the video system
	VIDEO_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);
	g_fb_width = rmode->fbWidth;
	g_fb_height = rmode->xfbHeight;

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	// Initialise the console, required for printf
	console_init(xfb,20,20,g_fb_width,g_fb_height,g_fb_width*VI_DISPLAY_PIX_SZ);
	
	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);
	
	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	/*************************************************************************
	 * AUDIO                                                                 *
	 *************************************************************************/
	// Initialise the audio subsysten
	AESND_Init(NULL);
	MODPlay_Init(&sound);
	MODPlay_SetMOD(&sound, sound_mod);
	MODPlay_SetVolume(&sound, 63,63);

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

void DrawHLine (int x1, int x2, int y, int color) {
    int i;
    y = 320 * y;
    x1 >>= 1;
    x2 >>= 1;
    for (i = x1; i <= x2; i++) {
        u32 *tmpfb = xfb;
        tmpfb[y+i] = color;
    }
}

void DrawVLine (int x, int y1, int y2, int color) {
    int i;
    x >>= 1;
    for (i = y1; i <= y2; i++) {
        u32 *tmpfb = xfb;
        tmpfb[x + ((640 * i) >> 1)] = color;
    }
}

void DrawBox (int x1, int y1, int x2, int y2, int color) {
    DrawHLine (x1, x2, y1, color);
    DrawHLine (x1, x2, y2, color);
    DrawVLine (x1, y1, y2, color);
    DrawVLine (x2, y1, y2, color);
}

void DrawParticle(int x, int y, int width, int height, int color) {
	int i;
	for(i=0; i<height; i++)
		DrawHLine(x, x+width, y+i, color);
}

void initParticles() {
	srand(time(NULL));
	int i;
	for(i=0; i<NUM_PARTICLES; i++) {
		// Create particle with random size, position and speed
		g_particles[i].size_x = rand() % PARTICLE_MAX_WIDTH;
		g_particles[i].size_y = rand() % PARTICLE_MAX_HEIGHT;
		g_particles[i].pos_x = rand() % (g_fb_width-g_particles[i].size_x);
		g_particles[i].pos_y = rand() % (g_fb_height-g_particles[i].size_y);
		g_particles[i].dx = (rand() % PARTICLE_MAX_SPEED) + PARTICLE_MIN_SPEED;
		g_particles[i].dy = (rand() % PARTICLE_MAX_SPEED) + PARTICLE_MIN_SPEED;

		if(rand() % 1) g_particles[i].dx = -g_particles[i].dx;
		if(rand() % 1) g_particles[i].dy = -g_particles[i].dy;
	}
}

void updateParticles() {

	int i;
	for(i=0; i<NUM_PARTICLES; i++) {

		g_particles[i].pos_x += g_particles[i].dx;
		g_particles[i].pos_y += g_particles[i].dy;

		// Check for collisions with screen boundaries
		if(g_particles[i].pos_x < 1 || g_particles[i].pos_x >= (g_fb_width-g_particles[i].size_x)) {
			g_particles[i].dx = -g_particles[i].dx;
			MODPlay_Stop(&sound);
			MODPlay_Start(&sound);
//			MODPlay_TriggerNote(&sound, 1 , 1, 1, 63);
		}

		if(g_particles[i].pos_y < 1 || g_particles[i].pos_y >= (g_fb_height-g_particles[i].size_y)) {
			g_particles[i].dy = -g_particles[i].dy;
			MODPlay_Stop(&sound);
			MODPlay_Start(&sound);
//			MODPlay_TriggerNote(&sound, 1 , 1, 1, 63);
		}

		// Display particle
		DrawParticle(g_particles[i].pos_x, g_particles[i].pos_y,
					 g_particles[i].size_x, g_particles[i].size_y,
					 COLOR_WHITE);
	}
}
