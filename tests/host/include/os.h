/* The parts of Ndless's os.h the gpSP frontend uses, for the PC test build
 * (tests/host). The calculator's own are simulated in host_hw.c. */
#ifndef HOST_OS_H
#define HOST_OS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
  unsigned char contact, proximity;
  unsigned short x, y;
  unsigned char x_velocity, y_velocity;
  unsigned short dummy;
  unsigned char pressed, arrow;
} touchpad_report_t;

typedef enum
{
  TPAD_ARROW_NONE, TPAD_ARROW_UP, TPAD_ARROW_UPRIGHT, TPAD_ARROW_RIGHT, TPAD_ARROW_RIGHTDOWN,
  TPAD_ARROW_DOWN, TPAD_ARROW_DOWNLEFT, TPAD_ARROW_LEFT, TPAD_ARROW_LEFTUP, TPAD_ARROW_CLICK
} tpad_arrow_t;

int touchpad_scan(touchpad_report_t *report);
int on_key_pressed(void);
void cfg_register_fileext(const char *ext, const char *prgm);
const char *get_documents_dir(void);
int NU_Current_Dir(const char *drive, char *path);
void refresh_osscr(void);

#define is_classic 0
#define is_cx2 1

#endif
