#ifndef __OSINT_H
#define __OSINT_H

extern char gbuffer[1024];


void emulator_init(int argc, char** argv);
/* Runs exactly one emulated frame. Returns 1 to keep running, 0 to quit. */
int  emulator_run();
void emulator_end();
void osint_render();

/* Set the emulation/present frame rate (host passes the monitor refresh, e.g. 60). */
void emulator_set_frame_rate(double hz);

/* Load a cartridge image (.vec/.bin) over the built-in BIOS, then reset. */
void emulator_load_cart(const char* utf8_path);
/* Reset the emulated machine (re-runs vecx_reset). */
void emulator_reset(void);

/* Load / clear a translucent color-gel overlay image (PNG/JPG) for OVERLAY2. */
void emulator_load_overlay(const char* utf8_path);
void emulator_clear_overlay(void);
/* 1 if an overlay image is currently loaded, else 0. */
int  emulator_has_overlay(void);
/* Toggle a video effect: 0=glow, 1=trail, 2=overlay. Returns new state (0/1), -1 if invalid. */
int  emulator_toggle_video(int which);
/* Current state of a video effect (0/1), -1 if invalid. */
int  emulator_get_video(int which);
/* Set a video effect on/off directly (used to restore saved per-game settings). */
void emulator_set_video(int which, int on);

/* Master audio volume, 0..100 (emulator-wide). */
int  emulator_get_volume(void);
void emulator_set_volume(int percent);

/* Ambient sound (looping flyback buzz), emulator-wide. */
int  emulator_has_ambient(void);          /* 1 if the flyback sample loaded */
int  emulator_get_ambient_enabled(void);
void emulator_set_ambient_enabled(int on);
int  emulator_get_ambient_volume(void);   /* 0..100 */
void emulator_set_ambient_volume(int pct);

/* Glow amount slider (0..15) and the three line/point size sliders (1.0..10.0). */
int   emulator_get_glow(void);
void  emulator_set_glow(int amt);
float emulator_get_line_width(void);   /* line stroke thickness */
void  emulator_set_line_width(float w);
float emulator_get_point_size(void);   /* rounded size at line endpoints */
void  emulator_set_point_size(float w);
float emulator_get_dot_size(void);     /* size of standalone (zero-length) dots */
void  emulator_set_dot_size(float w);

#endif

