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

#include "host_view.h"

HostViewRect host_fit_viewport(int clientW, int clientH, int aspectW, int aspectH) {
    HostViewRect r{ 0, 0, 0, 0 };
    if (clientW <= 0 || clientH <= 0 || aspectW <= 0 || aspectH <= 0) {
        return r;
    }
    // Compare clientW/clientH vs aspectW/aspectH by cross-multiplication
    // (64-bit to avoid overflow on large window sizes).
    if ((long long)clientW * aspectH <= (long long)clientH * aspectW) {
        // Width is the limiting dimension.
        r.w = clientW;
        r.h = (int)((long long)clientW * aspectH / aspectW);
    } else {
        // Height is the limiting dimension.
        r.h = clientH;
        r.w = (int)((long long)clientH * aspectW / aspectH);
    }
    r.x = (clientW - r.w) / 2;
    r.y = (clientH - r.h) / 2;
    return r;
}
