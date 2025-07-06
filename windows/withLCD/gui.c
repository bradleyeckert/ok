// GLUT-based GUI

#include "glut.h"
#include <stdio.h>
#include <stdint.h>
#include "TFTsim.h"
#include <windows.h>

#define MORESAFE

#define TFTX 240
#define TFTY 320
#define MARGIN 30
#define BTN0Y 20
#define WSIZEX TFTX + 2 * MARGIN
#define WSIZEY TFTY + 2 * MARGIN + BTN0Y

#define BUTTONS  1                  /* Number of buttons in the system       */
#define BTSIZEX  0.32f              /* Button size */
#define BTSIZEY  0.08f 

#define BTCOLOR1 0.3f, 0.3f, 0.3f   /* Color for button not pressed          */
#define BTCOLOR0 0.6f, 0.6f, 0.6f   /* Color for button pressed              */
#define LANEPITCH 0.24f
#define AACENTERX -0.25f            /* center of LCD AA in X                 */
#define AACENTERY -0.02f            /* center of LCD AA in Y                 */
#define AAWIDTH  0.55f
#define AAHEIGHT (AAWIDTH * 1.33f)

// The GL frame is centered at (0,0) and spans {-1,1} in both X and Y.

//##############################################################################
// Pushbuttons

struct Pushbutton_t {
    GLfloat x;
    GLfloat y;
    int state; // 0 = pressed, 1 = released
};

struct Pushbutton_t Buttons[BUTTONS];

static void LoadButton(int i, GLfloat x, GLfloat y) {
	if (i >= BUTTONS) return; // out of range
    Buttons[i].x = x;
    Buttons[i].y = y;
    Buttons[i].state = 1;
}

static void InitButtons(void) {
    LoadButton(0, -0.0f, -0.85f);
//  LoadButton(1, -0.1f, -(3 * LANEPITCH));
//  LoadButton(2, -0.54f, (3 * LANEPITCH));
//  LoadButton(3, -0.1f, (3 * LANEPITCH));
//  LoadButton(4,  0.5f, (3 * LANEPITCH));
//  LoadButton(5,  0.5f,  LANEPITCH);
//  LoadButton(6,  0.5f, -LANEPITCH);
//  LoadButton(7,  0.5f, -(3* LANEPITCH));
}

static void DrawButton(int i) {
    GLfloat x = Buttons[i].x;
    GLfloat y = Buttons[i].y;
    if (Buttons[i].state)
        glColor3f(BTCOLOR1);
    else
        glColor3f(BTCOLOR0);
    glBegin(GL_POLYGON);
    glVertex3f(-BTSIZEX + x, -BTSIZEY + y, 0.0);
    glVertex3f(-BTSIZEX + x,  BTSIZEY + y, 0.0);
    glVertex3f( BTSIZEX + x,  BTSIZEY + y, 0.0);
    glVertex3f( BTSIZEX + x, -BTSIZEY + y, 0.0);
    glEnd();
}

// Mouse clicks are referenced from the upper left in pixels.
// We are working in the relative frame, not pixels.
static void MyMouseFunc(int button, int state, int ix, int iy) {
    float x = -1.0f + 2.0f * (float) ix / glutGet(GLUT_WINDOW_WIDTH);
    float y =  1.0f - 2.0f * (float) iy / glutGet(GLUT_WINDOW_HEIGHT);
	uint32_t points[6] = { 0 }; // touch points
    for (int i = 0; i < BUTTONS; i++) {
        if (button == GLUT_LEFT_BUTTON) {
            if ((x > (Buttons[i].x - BTSIZEX))
                && (x < (Buttons[i].x + BTSIZEX))
                && (y > (Buttons[i].y - BTSIZEY))
                && (y < (Buttons[i].y + BTSIZEY))) {
                Buttons[i].state = state;
                points[0] = (state ^ 1) | 0x80000000;
                GUItouchHandler(0, 1, points);
            }
            else if (state == 0) {
                ix -= MARGIN;
                iy -= MARGIN;
                if (ix > 0 && ix < TFTX && iy > 0 && iy < TFTY) {
					// down-click in the LCD area, scale = pels
                    points[0] = ix | (iy << 16);
                }
                GUItouchHandler(1, 1, points);
            }
        }
    }
}

//##############################################################################
// LCD display simulator
// For TFTX x TFTY graphic LCD module ILI9341 controller with IM=0000.
// The raw data is held in Windows 24-bit BMP format with reversed red/blue.
// glDrawPixels does not support GL_BGR (native BMP) format.

#define LCDimageSize (3 * TFTX * TFTY + 56)
static uint8_t LCDimage[LCDimageSize];

// Load a bitmap from a file. Must be 24-bit, TFTX x TFTY.
void GUILCDload(char * s) {
    FILE* fp;
#ifdef MORESAFE
    errno_t err = fopen_s(&fp, s, "rb");
#else
    fp = fopen(s, "rb");
#endif
    if (fp == NULL) {
        memset(LCDimage, 0, sizeof(uint8_t) * LCDimageSize);
    }
    else {
        fread(LCDimage, LCDimageSize, 1, fp);
        // Swap the R and B bytes assuming no padding (width is multiple of 4)
        uint8_t* p = &LCDimage[54];     // skip the header
        for (int i = 0; i < (3 * TFTX * TFTY); i += 3) {
            uint8_t temp = p[2];
            p[2] = p[0];
            p[0] = temp;
            p += 3;
        }
        fclose(fp);
    }
}

//##############################################################################
// The continuously called display function
static void displayMe(void)
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);       // background
    for (int i = 0; i < BUTTONS; i++)   // buttons
        DrawButton(i);
    glColor3f(0.0f, 0.0f, 0.0f);        // dark LCD
    glRasterPos2f(AACENTERX - AAWIDTH, AACENTERY - AAHEIGHT); // lower left corner of LCD
    glDrawPixels(TFTX, TFTY, GL_RGB, GL_UNSIGNED_BYTE, &LCDimage[54]);
    glFlush();    glFlush();
    Sleep(10); // <-- windows.h dependency
}

// Don't let the window be re-sized.
static void MyReshape(int width, int height) {
    glutReshapeWindow(WSIZEX, WSIZEY);
}

uint16_t teststream[] = {
    0x12A, 0, 10,  0, 19, // Column Address Set
    0x12B, 0, 20,  0, 39, // Row Address Set
    0x12C, // Memory Write (16-bit data follows)
    0xFFFF };

int done = 0;

void GUIrun(void)
{
    char* myargv[1] = {NULL};
    int myargc = 1;
    myargv[0] = _strdup("glut");
    glutInit(&myargc, myargv);
    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(WSIZEX, WSIZEY);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("QVGA with Home Btn");
    InitButtons();
    glutDisplayFunc(displayMe);
    glutIdleFunc(displayMe);
    glutMouseFunc(MyMouseFunc);
    glutReshapeFunc(MyReshape);
    GUILCDload("splash.bmp");
    TFTLCDsetup(LCDimage, 0, TFTX, TFTY);
    while (!done) { // wait for the window to close
        glutMainLoopEvent();
    }
}

void GUIbye(void) {
    done = 1; // set the flag to exit the main loop
    glutLeaveMainLoop(); // exit the main loop
    glutDestroyWindow(glutGetWindow()); // destroy the window
}
