// Vectrex-Emu
// Copyright (C) 2026 Tim Cottrill and Claude Code
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <cstdio>
#include "host_view.h"

static int failures = 0;
static void check(const char* name, HostViewRect r, int x, int y, int w, int h) {
    bool ok = (r.x == x && r.y == y && r.w == w && r.h == h);
    printf("%-8s got {%d,%d,%d,%d} want {%d,%d,%d,%d} %s\n",
           name, r.x, r.y, r.w, r.h, x, y, w, h, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

int main() {
    check("exact", host_fit_viewport(384, 480, 4, 5),    0,   0, 384, 480);
    check("wide",  host_fit_viewport(800, 600, 4, 5),  160,   0, 480, 600);
    check("tall",  host_fit_viewport(480, 800, 4, 5),    0, 100, 480, 600);
    check("fhd",   host_fit_viewport(1920, 1080, 4, 5), 528,  0, 864, 1080);
    check("zero",  host_fit_viewport(0, 0, 4, 5),        0,   0,   0,   0);
    printf("%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
