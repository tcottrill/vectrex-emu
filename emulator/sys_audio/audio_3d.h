// =============================================================================
// audio_3d.h
// 2D positional audio via X3DAudio, for the voice path only.
//
// Coordinate system (OpenGL-style 2D):
//   +X = right on screen
//   +Y = up on screen
//   Listener is camera-locked: facing screen-up (+Y), top is +Y (out of screen
//   conceptually, but we use +Y for X3DAudio's "up" since this is a 2D world
//   with no real Z).
//
// The 2D world is mapped to X3DAudio's 3D space as (x, 0, y). Source above the
// listener on screen lands in the front speakers; source below lands in the
// rear surrounds.
//
// Init is called once after the audio backend is up. Calls after that point
// only require setting the listener (camera) position each frame and the
// source position per positional voice.
// =============================================================================
#pragma once

#include <cstdint>

#ifndef WIN7BUILD
#include <xaudio2.h>
#include <x3daudio.h>
#else
#include <xaudio2redist.h>
#include <x3daudio.h>
#endif

// Initialize X3DAudio for the current master speaker layout. channel_mask is
// SPEAKER_xxx bitmask from the mastering voice; dst_channels is its channel
// count. Idempotent. Returns true on success.
bool audio_3d_init(uint32_t channel_mask, uint32_t dst_channels);

// Tear down. Safe to call when not initialized.
void audio_3d_shutdown();

// True if init succeeded and positional APIs are usable.
bool audio_3d_ready();

// Update listener (camera) position in 2D world coords.
void audio_3d_set_listener_2d(float x, float y);

// Compute and apply X3DAudio output matrix for a source voice.
// src_channels is the voice's input channel count (1 mono, 2 stereo); only
// these two are supported. Returns true on success.
bool audio_3d_apply_2d(IXAudio2SourceVoice* voice,
	float src_x, float src_y,
	uint32_t src_channels);

// Diagnostic: prints the per-output-channel matrix on the NEXT call to
// audio_3d_apply_2d. Useful for verifying that source positions are producing
// the expected speaker routing.
void audio_3d_debug_print_next_matrix();

// Returns the channel mask actually passed to X3DAudioInitialize. May differ
// from the OS-reported endpoint mask if audio_3d_init normalized it (e.g.
// 5.1 SURROUND 0x60F -> legacy 5.1 0x3F). Zero before audio_3d_init succeeds.
uint32_t audio_3d_get_channel_mask();
