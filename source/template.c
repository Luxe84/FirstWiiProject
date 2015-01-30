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

// Global definitions
static void *g_xfb[2]; 				// external framebuffers, double buffering
int g_fbi=0;						// index of current framebuffer
static GXRModeObj *g_vmode = NULL;	// ref. to render mode object
int g_fb_height, g_fb_width;		// dimensions of external fb
s32 g_voice;						// voice handle
WPADData *g_wpd = NULL;				// for handling controller input
s32 g_shutDownType = -1;			// flag for callback functions
int evctr = 0;						// event counter
int g_simulate = 1;					// should the particle simulation run?

typedef struct {
	int pos_x, pos_y;   // screen coordinates
	int size_x, size_y; // dimensions
	int dx, dy;         // speed values
	int freq;		    // freequency of collision sound
}Particle;

Particle g_particles[NUM_PARTICLES];

// Callback functions

/*****************************************************************************
 * Directly load the Wii Channels menu, without actually cold-resetting the  *
 * system                                                                    *
 *****************************************************************************/
void cb_WiiResetButtonPressed() {
	g_shutDownType = SYS_RETURNTOMENU;
}

/*****************************************************************************
 * Powers off the Wii, automatically choosing Standby or Idle mode depending *
 * on the user's configuration                                               *
 *****************************************************************************/
void cb_WiiPowerButtonPressed() {
	g_shutDownType = SYS_POWEROFF;
}

/*****************************************************************************
 * Powers off the Wii to standby (red LED, WC24 off) mode                    *
 *****************************************************************************/
void cb_WiimotePowerButtonPressed(s32 chan) {
	g_shutDownType = SYS_POWEROFF_STANDBY;
}

/*****************************************************************************
 * General callback triggered by arbitrary Wiimote events                    *
 *****************************************************************************/
void cb_WiimoteEventFired(int chan, const WPADData *data) {
	evctr++;
	if(data->btns_d & WPAD_BUTTON_A) g_simulate^=1;
	else if(data->btns_d & WPAD_BUTTON_HOME) exit(0); // Return to loader

}

// Drawing

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

void drawPixel(int x, int y, int color) {
	u32 *tmpfb = g_xfb[g_fbi];
	x>>=1;
	y *= g_fb_width>>1;
	tmpfb[y+x] = color;
}

void drawHLine0 (int x1, int x2, int y, int color) {
    int i;
    y = 320 * y;
    x1 >>= 1;
    x2 >>= 1;
    for (i = x1; i <= x2; i++) {
        u32 *tmpfb = g_xfb[g_fbi];
        tmpfb[y+i] = color;
    }
}

void drawHLine (int x1, int x2, int y, int color) {
    int i;
    x1 >>= 1;
    x2 >>= 1;
    y *= g_fb_width>>1;
    u32 *tmpfb = g_xfb[g_fbi];
    tmpfb += y;
    for (i = x1; i <= x2; i++) {
        tmpfb[i] = color;
    }
}

void drawVLine0 (int x, int y1, int y2, int color) {
    int i;
    x >>= 1;
    for (i = y1; i <= y2; i++) {
        u32 *tmpfb = g_xfb[g_fbi];
        tmpfb[x + ((640 * i) >> 1)] = color;
    }
}

void drawVLine (int x, int y1, int y2, int color) {
    int i;
    x >>= 1;
    u32 *tmpfb = g_xfb[g_fbi];
    tmpfb += x;
    for (i = y1; i <= y2; i++) {
        tmpfb[(g_fb_width>>1)*i] = color;
    }
}

void drawLine(int x0, int y0, int x1, int y1, int color)
{
  int dx =  abs(x1-x0), sx = x0<x1 ? 1 : -1;
  int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
  int err = dx+dy, e2; /* error value e_xy */

  for(;;) {
	  drawPixel(x0,y0, color);
	  if (x0==x1 && y0==y1) break;
	  e2 = 2*err;
	  if (e2 > dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
	  if (e2 < dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
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

void drawdot(float w, float h, float fx, float fy, u32 color) {
	u32 *fb;
	int px,py;
	int x,y;
	fb = (u32*)g_xfb[g_fbi];

	y = fy * g_fb_height / h;
	x = fx * g_fb_width  / w / 2;

	for(py=y-4; py<=(y+4); py++) {
		if(py < 0 || py >= g_fb_height)
				continue;
		for(px=x-2; px<=(x+2); px++) {
			if(px < 0 || px >= g_fb_width/2)
				continue;
			fb[g_fb_width/VI_DISPLAY_PIX_SZ*py + px] = color;
		}
	}

}

void drawEllipse(int xm, int ym, int a, int b, int color)
{
   int dx = 0, dy = b;
   long a2 = a*a, b2 = b*b;
   long err = b2-(2*b-1)*a2, e2;

   do {
       drawPixel(xm+dx, ym+dy, color);
       drawPixel(xm-dx, ym+dy, color);
       drawPixel(xm-dx, ym-dy, color);
       drawPixel(xm+dx, ym-dy, color);

       e2 = 2*err;
       if (e2 <  (2*dx+1)*b2) { dx++; err += (2*dx+1)*b2; }
       if (e2 > -(2*dy-1)*a2) { dy--; err -= (2*dy-1)*a2; }
   } while (dy >= 0);

   while (dx++ < a) {
	   drawPixel(xm+dx, ym, color);
	   drawPixel(xm-dx, ym, color);
   }
}

void displayIR() {
	int i;
	float theta;

	// IR dots
	for(i=0; i<4; i++) {
		if(g_wpd->ir.dot[i].visible) {
			drawdot(1024, 768, g_wpd->ir.dot[i].rx, g_wpd->ir.dot[i].ry, COLOR_YELLOW);
		}
	}
	// Sensor bar as it is seen by the wiimote
	if(g_wpd->ir.raw_valid) {
		for(i=0; i<2; i++) {
			drawdot(4, 4, g_wpd->ir.sensorbar.rot_dots[i].x+2, g_wpd->ir.sensorbar.rot_dots[i].y+2, COLOR_GREEN);
		}
	}
	// Cursor
	if(g_wpd->ir.valid) {
		theta = g_wpd->ir.angle / 180.f * M_PI;
		drawdot(g_fb_width, g_fb_height, g_wpd->ir.x, g_wpd->ir.y, COLOR_RED);
		drawdot(g_fb_width, g_fb_height, g_wpd->ir.x + 10*sinf(theta), g_wpd->ir.y - 10*cosf(theta), COLOR_BLUE);
	}
}

// Init and update routines

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
			g_voice=ASND_GetFirstUnusedVoice();
			ASND_SetVoice(g_voice, VOICE_MONO_16BIT, g_particles[i].freq, 0,
						 (u8 *)sound_pcm, sound_pcm_size, 63, 63, NULL);
		}

		if(g_particles[i].pos_y < 0 || g_particles[i].pos_y > (g_fb_height-g_particles[i].size_y)) {
			g_particles[i].dy = -g_particles[i].dy;
			g_particles[i].pos_y = (g_particles[i].pos_y<0)?0:g_fb_height-g_particles[i].size_y;
			g_voice=ASND_GetFirstUnusedVoice();
			ASND_SetVoice(g_voice, VOICE_MONO_16BIT, g_particles[i].freq, 0,
						 (u8 *)sound_pcm, sound_pcm_size, 63, 63, NULL);
		}

		// Display updated particle
		drawParticle(g_particles[i].pos_x,  g_particles[i].pos_y,
					 g_particles[i].pos_x + g_particles[i].size_x - 1,
					 g_particles[i].pos_y + g_particles[i].size_y - 1,
					 COLOR_WHITE);
	}
}

void printVideoInfo() {

	if(g_vmode!=NULL)
	{
		// The console understands VT terminal escape codes
		// This positions the cursor on row 2, column 1
		// we can use variables for this with format codes too
		// e.g. printf ("\x1b[%d;%dH", row, column );
		printf("\x1b[2;1H");

		printf("Hello World!\n\n");

		printf("\taa:        %d\n", g_vmode->aa);
		printf("\tfbWidtht:  %d\n", g_vmode->fbWidth);
		printf("\tefbHeight: %d\n", g_vmode->efbHeight);
		printf("\txfbHeight: %d\n", g_vmode->xfbHeight);
		printf("\txfbMode:   %d\n", g_vmode->xfbMode);
		printf("\tviWidth:   %d\n", g_vmode->viWidth);
		printf("\tviHeight:  %d\n", g_vmode->viHeight);
		printf("\tviTVMode:  %d\n", g_vmode->viTVMode);
		printf("\tviXOrigin: %d\n", g_vmode->viXOrigin);
		printf("\tviYOrigin: %d\n", g_vmode->viYOrigin);
	}
}

void printWiimoteinfo() {
	if(g_wpd!=NULL) {
		int i;
		printf("\n");
		printf(" Event count: %d\n",evctr);
		printf(" Battery Level: %d\n", g_wpd->battery_level);
		printf(" Data->Err: %d\n",g_wpd->err);
		printf(" IR Dots:\n");
		for(i=0; i<4; i++) {
			if(g_wpd->ir.dot[i].visible) {
				printf(" %4d, %3d\n", g_wpd->ir.dot[i].rx, g_wpd->ir.dot[i].ry);
			}
			else {
				printf(" None\n");
			}
		}
		if(g_wpd->ir.valid) {
			printf(" Cursor: %.02f, %.02f\n", g_wpd->ir.x, g_wpd->ir.y);
			printf(" Angle: %.02f deg\n", g_wpd->ir.angle);
		}
		else {
			printf(" No Cursor\n\n");
		}
		if(g_wpd->ir.raw_valid) {
			printf(" Distance: %.02fm\n", g_wpd->ir.z);
			printf(" Yaw: %.02f deg\n", g_wpd->orient.yaw);
		} else {
			printf("\n\n");
		}
		printf(" Accel:\n");
		printf(" XYZ: %3d,%3d,%3d\n",g_wpd->accel.x,g_wpd->accel.y,g_wpd->accel.z);
		printf(" Pitch: %.02f\n",g_wpd->orient.pitch);
		printf(" Roll: %.02f\n",g_wpd->orient.roll);
		printf(" Buttons down:\n ");
		if(g_wpd->btns_h & WPAD_BUTTON_A) printf("A ");
		if(g_wpd->btns_h & WPAD_BUTTON_B) printf("B ");
		if(g_wpd->btns_h & WPAD_BUTTON_1) printf("1 ");
		if(g_wpd->btns_h & WPAD_BUTTON_2) printf("2 ");
		if(g_wpd->btns_h & WPAD_BUTTON_MINUS) printf("MINUS ");
		if(g_wpd->btns_h & WPAD_BUTTON_HOME) printf("HOME ");
		if(g_wpd->btns_h & WPAD_BUTTON_PLUS) printf("PLUS ");
		printf("\n ");
		if(g_wpd->btns_h & WPAD_BUTTON_LEFT) printf("LEFT ");
		if(g_wpd->btns_h & WPAD_BUTTON_RIGHT) printf("RIGHT ");
		if(g_wpd->btns_h & WPAD_BUTTON_UP) printf("UP ");
		if(g_wpd->btns_h & WPAD_BUTTON_DOWN) printf("DOWN ");
		printf("\n");
	}
}

void init() {

	/*************************************************************************
	 * VIDEO                                                                 *
	 *************************************************************************/
	// Initialise the video system
	VIDEO_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	g_vmode = VIDEO_GetPreferredMode(NULL);

	// widescreen fix
	if(CONF_GetAspectRatio() == CONF_ASPECT_16_9)
		g_vmode->viWidth = VI_MAX_WIDTH_PAL;

	// Allocate memory for the display in the uncached region
	g_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_vmode));
	g_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_vmode));
	g_fb_width = g_vmode->fbWidth;
	g_fb_height = g_vmode->xfbHeight;

	// Set up the video registers with the chosen mode
	VIDEO_Configure(g_vmode);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(g_xfb[g_fbi]);

	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(g_vmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	/*************************************************************************
	 * AUDIO                                                                 *
	 *************************************************************************/
	// Initialise the audio subsysten
	AUDIO_Init(NULL);
	ASND_Init(NULL);
	ASND_Pause(0);
	PlayOgg(bg_music_ogg, bg_music_ogg_size, 0, OGG_INFINITE_TIME);

	/*************************************************************************
	 * CONTROLS                                                              *
	 *************************************************************************/
	// This function initialises the attached controllers
	WPAD_Init();

	//	Configure infrared system of the Wii remote
	WPAD_SetVRes(WPAD_CHAN_0, g_fb_width, g_fb_height);
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

	// Install callbacks
	SYS_SetResetCallback(cb_WiiResetButtonPressed);
	SYS_SetPowerCallback(cb_WiiPowerButtonPressed);
	WPAD_SetPowerButtonCallback(cb_WiimotePowerButtonPressed);

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
	int ret;
	u32 type;

	// Initialization
	init();

	// Game loop
	while(g_shutDownType==-1) {
		// Setup console, required for printf
		console_init(g_xfb[g_fbi], 0, 0, g_fb_width, g_fb_height,
					 g_fb_width * VI_DISPLAY_PIX_SZ);
		VIDEO_ClearFrameBuffer(g_vmode, g_xfb[g_fbi], COLOR_BLACK);
		//printVideoInfo();

		// Check status of the Wiimote
		WPAD_ReadPending(WPAD_CHAN_ALL, cb_WiimoteEventFired);
		ret = WPAD_Probe(WPAD_CHAN_0, &type);
		switch(ret) {
			case WPAD_ERR_NO_CONTROLLER:
				printf(" Wiimote not connected\n");
				break;
			case WPAD_ERR_NOT_READY:
				printf(" Wiimote not ready\n");
				break;
			case WPAD_ERR_NONE:
				printf(" Wiimote ready\n");
				break;
			default:
				printf(" Unknown Wiimote status %d\n", ret);
		}

		// If no error occured, process retrieved controller input
		if(ret == WPAD_ERR_NONE)
		{
			g_wpd = WPAD_Data(WPAD_CHAN_0);
			printWiimoteinfo();
			displayIR();
		}

		// Display background
		drawBox((g_fb_width>>1)-10, (g_fb_height>>1)-10,
				(g_fb_width>>1)+10, (g_fb_height>>1)+10, COLOR_WHITE);
		drawBox(0, 0, g_fb_width-1, g_fb_height-1, COLOR_WHITE);
		drawVLine(g_fb_width>>1, 0, g_fb_height-1, COLOR_WHITE);


		drawLine(100, 100, 200, 300, COLOR_WHITE);
		drawEllipse(200, 200, 20, 10, COLOR_WHITE);
		drawCircle(g_fb_width>>1, g_fb_height>>1, 10, COLOR_WHITE);


		// Update game engine and render changes
		if(g_simulate)
			updateParticles();

		// Wait for the next frame and switch framebuffer
		VIDEO_SetNextFramebuffer(g_xfb[g_fbi]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		g_fbi^=1;
	}

	// Perform invoked shutdown of the application
	SYS_ResetSystem(g_shutDownType, 0, 0);

	return 0;
}
