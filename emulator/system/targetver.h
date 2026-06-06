// Target Windows 7 SP1 so we can run on low end systems.
#include <winsdkver.h>
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <sdkddkver.h>