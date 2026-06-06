#ifndef __OSSTUFF__
#define __OSSTUFF__


//Keyboard Led Handling from M.A.M.E(TM)
void osd_set_leds(int state);
int osd_get_leds();

void SetTopMost(const bool TopMost);
void setwindow();
void center_window();


void ClientResize(int nWidth, int nHeight);
void GetDesktopResolution(int& horizontal, int& vertical);
void GetRefresh();
void LimitThreadAffinityToCurrentProc();

#endif

