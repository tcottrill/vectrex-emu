#pragma once
#include <cstdint>
// -----------------------------------------------------------------------------
// cpu_control.h (vecx shim)
//
// Minimal stand-in for the AAE cpu_control layer. The AY-3-8910 core uses
// cpu_scale_by_cycles() for its mid-frame "catch-up" rendering: it maps the
// CPU's cycle position within the current frame onto a position in the audio
// sample buffer. The real implementation lives in vecx.cpp and is backed by the
// emulated audio-block cycle counter, independent of video refresh.
// -----------------------------------------------------------------------------

int cpu_scale_by_cycles(int val, int clock);

// The full 64K Vectrex address space and the CPU instance, shared between
// vecx.cpp (owns the memory-map handlers / CPU) and emulator.cpp (loads the
// BIOS + cart images into it). Layout: cart 0x0000-0x7FFF, RAM 1K at 0xC800
// (mirrored), VIA 0xD000-0xD7FF, system BIOS 0xE000-0xFFFF.
class cpu_m6809;
extern uint8_t    g_cpu_mem[0x10000];
extern cpu_m6809* g_cpu;
