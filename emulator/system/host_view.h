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

#pragma once

// Pure, Win32-free aspect-fit math so it can be unit-tested standalone.
struct HostViewRect { int x, y, w, h; };

// Largest rect of aspect aspectW:aspectH that fits inside clientW x clientH,
// centered. Returns {0,0,0,0} if any input is <= 0.
HostViewRect host_fit_viewport(int clientW, int clientH, int aspectW, int aspectH);
