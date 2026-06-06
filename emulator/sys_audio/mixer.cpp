/* =============================================================================
 * File: mixer.cpp
 * Component: Audio mixer + playback (XAudio2 backend)
 *
 * Overview
 * --------
 * Hybrid audio system providing two playback paths:
 *   1) Per-voice playback (one IXAudio2SourceVoice per channel) for simple,
 *      low-latency one-shot or looping samples.
 *   2) Lightweight software mixer that mixes MONO/STEREO 8/16-bit PCM into an
 *      interleaved S16 buffer submitted to XAudio2 each frame (no callback).
 *
 * The mixer supports fractional update rates (e.g., 29.97/59.94 fps) via a
 * 32.32 fixed-point samples-per-frame accumulator to eliminate long-term drift.
 * Optional VU meters provide smooth left/right peak levels for UI display.
 *
 * Key Types (see mixer.h)
 * -----------------------
 *   CHANNEL : Runtime state of a logical channel (voice, buffer, loop, vol/pan/pitch).
 *   SAMPLE  : Loaded audio (8/16-bit PCM, mono/stereo) + format metadata.
 *
 * Runtime Model
 * -------------
 *   - Call mixer_init(outputRateHz, fpsExact) once at startup.
 *   - Call mixer_update() once per game/app tick; it fills the next output buffer
 *     and hands it to the streaming backend.
 *   - Use the voice path (sample_start/stop/end) for direct XAudio2 playback,
 *     or use the software-mixer path (sample_start_mixer / stream_*) for mixed or
 *     generated audio that you update every frame.
 *
 * Panning & Volume
 * ----------------
 *   - Pan uses a constant-power style mapping from 0..255 (128 = center) to L/R gains.
 *   - Volume accepts 0..255 (or 0..100 via helpers) and is mapped to a perceptual
 *     (dB-tapered) linear amplitude for consistent loudness control.
 *
 * Threading
 * ---------
 *   - A small audio worker is signaled once per frame to run mixer_update_internal().
 *     Synchronization uses a mutex + condition variable. Optional watchdog logging
 *     helps detect stalls or starvation.
 *
 * Most Important Functions
 * ------------------------
 *   // Initialization & lifecycle
 *   void mixer_init(int rate, double fps);
 *       Initializes mixer state and the XAudio2 streaming backend; computes the
 *       frame buffer capacity from (rate / fps).
 *
 *   void mixer_update();
 *       Mixes all active software-mixer voices into the next interleaved S16 buffer
 *       and submits it to the backend.
 *
 *   void mixer_end();
 *       Shuts down playback, releases voices/buffers, and resets mixer state.
 *
 *   // Sample management (loading/saving)
 *   int  load_sample(const char* archname, const char* filename, bool force_resample = true);
 *       Loads audio into a SAMPLE and registers it; optionally resamples to the
 *       system output rate on load.
 *   void save_sample(int samplenum);
 *       Writes a loaded SAMPLE back to disk as a standard WAV.
 *
 *   // Voice path (direct XAudio2 per-channel playback)
 *   void sample_start(int chanid, int samplenum, int loop);
 *   void sample_stop(int chanid);
 *   void sample_end(int chanid);
 *   int  sample_playing(int chanid);
 *       Create/queue/stop source-voice playback for a channel; query playing state.
 *
 *   void sample_set_volume(int chanid, int volume /0..255/);
 *   void sample_set_freq(int chanid, int freq /Hz/);
 *   void sample_set_pan(int chanid, int pan /0..255, 128=center/);
 *   int  sample_get_volume(int chanid);
 *   int  sample_get_freq(int chanid);
 *   int  sample_get_pan(int chanid);
 *       Per-channel volume/pitch/pan control.
 *
 *   // Software-mixer path (mixed in mixer_update)
 *   void sample_start_mixer(int chanid, int samplenum, int loop);
 *   void sample_stop_mixer(int chanid);
 *   void sample_end_mixer(int chanid);
 *       Start/stop a SAMPLE that will be mixed by the software mixer each frame.
 *
 *   // Simple streaming (producer overwrites a fixed buffer each update)
 *   void stream_start(int chanid, int stream, int bits, int frame_rate);
 *   void stream_start(int chanid, int stream, int bits, int frame_rate, bool stereo);
 *   void stream_update(int chanid, short* data);
 *   void stream_update(int chanid, unsigned char* data);
 *   void stream_stop(int chanid, int stream);
 *       Treat a channel as a ring-updated SAMPLE for live/generated audio.
 *
 *   // Utilities & helpers
 *   int  create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name);
 *       Allocate a SAMPLE buffer for generated/streamed audio and register it.
 *   void resample_wav_8(SAMPLE* s, int new_freq);
 *   void resample_wav_16(SAMPLE* s, int new_freq, bool use_cubic = true);
 *       Load-time/utility resamplers used to conform assets to the output rate.
 *   float mixer_get_master_volume();
 *   void  mixer_set_master_volume(int volumePercent);
 *       Master output gain (implemented by the XAudio2 streaming backend).
 *
 * Dependencies
 * ------------
 *   - XAudio2 streaming backend (XAudio2Stream.*) for buffer submission.
 *   - WAV/MP3/OGG loader (wav_file.*) feeding the SAMPLE structure.
 *   - mixer.h for public API/types; mixer_volume.h for dB/linear conversions.
 *
 * Build Notes
 * -----------
 *   - Windows + XAudio2 (xaudio2redist). Link XAudio2 and include the Windows SDK.
 *
 * Limitations
 * -----------
 *   - Default software path mixes to interleaved S16 stereo.
 *   - Pitch changes on the voice path use XAudio2 FrequencyRatio.
 *
 * ---------------------------------------------------------------------------
 * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2022-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */
 // This code was updated with assistance from chatgpt
 // Note for ME: This is the FULL INTEGER version of this code, for AAE only: 8/15/25

#include "mixer.h"
#include "xaudio2_backend.h"
#include "audio_3d.h"
#include "error_wav.h"
#include "sys_log.h"
#include <mutex>
#include <vector>
#include <list>
#include <atomic>
#include <cmath>
#include <memory>
#include <cstring>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <unordered_map>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

//#define OGG_DECODE
//#define MP3_DECODE

#ifdef OGG_DECODE
#include "stb_vorbis.h"
#endif

#ifdef MP3_DECODE
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

static int SYS_FREQ = 44100;
static int BUFFER_SIZE = 0;
constexpr int MAX_CHANNELS = 20;
constexpr int MAX_SOUNDS = 255;
static std::atomic<bool> sound_paused{ false };
static int sound_id = -1;
static int g_master_pct = 80;          // canonical master volume in percent (0..100)
static int last_master_pct = 80;       // saved by pause_audio, restored by restore_audio
static std::mutex audioMutex;
static std::list<int> audio_list;
static std::vector<std::shared_ptr<SAMPLE>> lsamples;
static CHANNEL channel[MAX_CHANNELS];

// Streaming backend - owns the engine, mastering voice, output source voice,
// and ring buffers. Voice path uses g_xaudio2 (cached from g_backend at init)
// to call CreateSourceVoice directly for per-channel XAudio2 playback.
static std::unique_ptr<IAudioBackend> g_backend;
static IXAudio2* g_xaudio2 = nullptr;

// Positional audio listener (camera) in 2D world coords. Mirrored into
// audio_3d module on every change so the per-frame update can read it without
// querying back.
static float g_listener_x = 0.0f;
static float g_listener_y = 0.0f;
static bool  g_3d_inited = false;

#ifdef USE_VUMETER
// VU METER ONLY
// Smooth peak meters (0..1), written by audio thread, read by main thread.
static std::atomic<float> g_vuL{ 0.0f };
static std::atomic<float> g_vuR{ 0.0f };

// Simple peak meter ballistics: fast attack (take peak immediately) and slow decay.
static inline float vu_decay_step(float prev, float target, float decayFactor)
{
	// Attack: jump up immediately to new peak
	if (target > prev) return target;
	// Release: multiply by decayFactor (~0.90..0.98 per update @60Hz)
	return prev * decayFactor;
}
#endif

// ----------------------
// Thread management
// ----------------------
static std::thread audioThread;
static std::condition_variable audioCV;
static std::mutex audioCVMutex;
static std::atomic<bool> audioThreadExit{ false };
static std::atomic<bool> audioThreadRun{ false };
// FIX: Add flag to track if mixer is initialized (thread is running)
static std::atomic<bool> audioThreadActive{ false };

// Safeguards
static std::atomic<int> queuedFrames{ 0 };
static std::chrono::steady_clock::time_point lastSignalTime;

// Forward declarations
static void mixer_update_internal();
static void stop_channel_locked(int chanid);

// 0..255 (128=center).
// Uses "Square Root" law for constant power (volume stays consistent across pan).
static inline void mixer_pan_gains(int panByte, float& gainL, float& gainR)
{
	panByte = std::clamp(panByte, 0, 255);

	// Map 0..255 to 0.0..1.0 (approximate PI/2 curve)
	// 0 = Left, 128 = Center, 255 = Right
	float pan = panByte / 255.0f;

	// Constant power calculation
	// Left = cos(pan * PI/2), Right = sin(pan * PI/2)
	// This ensures Left^2 + Right^2 = 1.0 (constant energy)
	float angle = pan * (static_cast<float>(M_PI) / 2.0f);

	gainL = std::cos(angle);
	gainR = std::sin(angle);
}

// -----------------------------------------------------------------------------
// Audio Thread Function
// Waits for a signal each frame, then runs mixer_update_internal().
// Includes timing and watchdog logging.
// -----------------------------------------------------------------------------
static void audio_thread_func() {
	LOG_INFO("Audio thread: started");
	std::unique_lock<std::mutex> lock(audioCVMutex);

	while (!audioThreadExit.load(std::memory_order_acquire)) {
		// Wait with timeout to detect long idle times
		bool woke = audioCV.wait_for(lock, std::chrono::seconds(1), [] {
			return audioThreadExit.load(std::memory_order_acquire) || audioThreadRun.load(std::memory_order_acquire);
			});
		
		// Check exit flag immediately after waking
		if (audioThreadExit.load(std::memory_order_acquire)) break;
		
		if (!woke) {
			LOG_INFO("Audio thread: waited >1s without signal (emulator paused?)");
			continue;
		}

		// Double-check exit flag before processing
		if (audioThreadExit.load(std::memory_order_acquire)) break;
		
		// Check if we should actually run (audioThreadRun may be false if we woke due to exit)
		if (!audioThreadRun.load(std::memory_order_acquire)) continue;

		// Check if frames are piling up
		if (queuedFrames > 1) {
			LOG_INFO("Audio thread: %d frames queued (main thread may be signaling too fast)", queuedFrames.load());
		}

		// Check how long since last signal
		auto now = std::chrono::steady_clock::now();
		auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSignalTime).count();
		if (delta > 50) {
			LOG_INFO("Audio thread: WARNING - late signal detected (%lld ms since last frame)", (long long)delta);
		}

		// Measure execution time
		auto start = std::chrono::high_resolution_clock::now();
		mixer_update_internal();
		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

		if (elapsed > 2000) { // log if >2ms
			LOG_INFO("Audio thread: mixer_update_internal() took %lld microseconds", (long long)elapsed);
		}

		queuedFrames = 0;
		audioThreadRun.store(false, std::memory_order_release);
	}

	LOG_INFO("Audio thread: exiting");
}

unsigned char Make8bit(int16_t sample)
{
	sample >>= 8;
	sample ^= 0x80;
	return static_cast<uint8_t>(sample & 0xFF);
}

short Make16bit(uint8_t sample)
{
	return static_cast<int16_t>(sample - 0x80) << 8;
}

// -----------------------  RESAMPLING CODE BELOW --------------------------------------------------

// Helper: deinterleave / interleave
template<typename T>
static void deinterleave(const T* src, int frames, int ch, std::vector<std::vector<T>>& chans)
{
	chans.assign(ch, std::vector<T>(frames));
	for (int i = 0; i < frames; ++i)
		for (int c = 0; c < ch; ++c)
			chans[c][i] = src[i * ch + c];
}

template<typename T>
static void interleave(const std::vector<std::vector<T>>& chans, int frames, int ch, T* dst)
{
	for (int i = 0; i < frames; ++i)
		for (int c = 0; c < ch; ++c)
			dst[i * ch + c] = chans[c][i];
}

// 8-bit (unsigned) resample: linear only (robust and cheap)
void resample_wav_8(SAMPLE* sample, int new_freq)
{
	if (!sample || !sample->data8) return;

	const int ch = std::max<int>(1, sample->fx.nChannels);
	const int in_samples = static_cast<int>(sample->sampleCount);      // total samples (includes both channels)
	const int in_frames = in_samples / ch;
	if (in_frames <= 0 || sample->fx.nSamplesPerSec <= 0) return;

	const int out_frames = static_cast<int>((int64_t)in_frames * new_freq / sample->fx.nSamplesPerSec);
	if (out_frames <= 0) return;

	// Deinterleave
	std::vector<std::vector<uint8_t>> chans;
	deinterleave(sample->data8.get(), in_frames, ch, chans);

	// Resample each channel
	std::vector<std::vector<uint8_t>> chans_out(ch, std::vector<uint8_t>(out_frames));
	for (int c = 0; c < ch; ++c) {
		linear_interpolation_8(chans[c].data(), chans_out[c].data(), in_frames, out_frames);
	}

	// Interleave to a new owned buffer
	auto out = std::make_unique<uint8_t[]>(out_frames * ch);
	interleave(chans_out, out_frames, ch, out.get());

	// Publish
	sample->data8 = std::move(out);
	sample->data16.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = out_frames * ch;                   // bytes (8-bit)
	sample->sampleCount = out_frames * ch;               // total samples
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data8.get();

	LOG_INFO("Resampled 8-bit Sample #%d (%s): %d ch, %d -> %d Hz, %d -> %d frames",
		sample->num, sample->name.c_str(), ch,
		(int)sample->fx.nSamplesPerSec, new_freq, in_frames, out_frames);
}

// 16-bit resample: cubic by default for better quality (falls back easy)
void resample_wav_16(SAMPLE* sample, int new_freq, bool use_cubic /*= true*/)
{
	if (!sample || !sample->data16) return;

	const int ch = std::max<int>(1, sample->fx.nChannels);
	const int in_samples = static_cast<int>(sample->sampleCount);      // total samples (includes both channels)
	const int in_frames = in_samples / ch;
	if (in_frames <= 0 || sample->fx.nSamplesPerSec <= 0) return;

	const int out_frames = static_cast<int>((int64_t)in_frames * new_freq / sample->fx.nSamplesPerSec);
	if (out_frames <= 0) return;

	// Deinterleave
	std::vector<std::vector<int16_t>> chans;
	deinterleave(sample->data16.get(), in_frames, ch, chans);

	// Resample each channel into owned storage
	std::vector<std::vector<int16_t>> chans_out(ch, std::vector<int16_t>(out_frames));
	for (int c = 0; c < ch; ++c) {
		if (use_cubic)
			cubic_interpolation_16_into(chans[c].data(), in_frames, chans_out[c].data(), out_frames);
		else
			linear_interpolation_16_into(chans[c].data(), in_frames, chans_out[c].data(), out_frames);
	}

	// Interleave to a new owned buffer
	auto out = std::make_unique<int16_t[]>(out_frames * ch);
	interleave(chans_out, out_frames, ch, out.get());

	// Publish
	sample->data16 = std::move(out);
	sample->data8.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = out_frames * ch * sizeof(int16_t);
	sample->sampleCount = out_frames * ch;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data16.get();

	LOG_INFO("Resampled 16-bit Sample #%d (%s): %d ch, %s, %d -> %d Hz, %d -> %d frames",
		sample->num, sample->name.c_str(), ch, (use_cubic ? "cubic" : "linear"),
		(int)sample->fx.nSamplesPerSec, new_freq, in_frames, out_frames);
}

// -----------------------  END RESAMPLING CODE --------------------------------------------------

// -----------------------------------------------------------------------------
// load_sample_from_buffer
// Load a WAV sample from a memory buffer containing raw WAV file data.
// The caller is responsible for reading the file into the buffer using their
// own file I/O utilities.
//
// Parameters:
//   data           - pointer to WAV file data in memory
//   size           - size of the data buffer in bytes
//   name           - sample name for identification/logging (optional, can be nullptr
//                    or empty string - will auto-generate "sample_N" if not provided)
//   force_resample - if true, resample to system frequency on load (default: true)
//
// Returns:
//   Sample ID on success (>= 0), -1 on failure
// -----------------------------------------------------------------------------
int load_sample_from_buffer(const uint8_t* data, size_t size, const char* name, bool force_resample)
{
	if (!data || size == 0) {
		LOG_ERROR("load_sample_from_buffer: invalid buffer (data=%p, size=%zu)", (const void*)data, size);
		return -1;
	}

	auto sample = std::make_shared<SAMPLE>();
	HRESULT result = S_OK;

	result = WavLoadFileInternal(const_cast<unsigned char*>(data), static_cast<int>(size), sample.get());

	if (FAILED(result)) {
		LOG_ERROR("Error loading WAV from buffer: %s", name ? name : "(unnamed)");
		// Fallback to error.wav if provided
		result = WavLoadFileInternal(error_wav, sizeof(error_wav), sample.get());
		if (FAILED(result)) {
			LOG_ERROR("Failed to load fallback error.wav");
			return -1;
		}
	}

	sample->state = SoundState::Loaded;
	sample->num = ++sound_id;
	
	// Use provided name if valid, otherwise auto-generate from sample ID
	if (name && name[0] != '\0') {
		sample->name = name;
	} else {
		sample->name = "sample_" + std::to_string(sample->num);
	}

	if (force_resample && sample->fx.nSamplesPerSec != SYS_FREQ) {
		if (sample->fx.wBitsPerSample == 8) resample_wav_8(sample.get(), SYS_FREQ);
		else                                resample_wav_16(sample.get(), SYS_FREQ, /*use_cubic=*/true);
	}

	LOG_INFO("Loaded sample '%s' with ID %d", sample->name.c_str(), sample->num);
	{
		std::scoped_lock lock(audioMutex);
		lsamples.push_back(sample);
	}
	LOG_INFO("Sample load completed");
	return sample->num;
}

// -----------------------------------------------------------------------------
// mixer_upload_sample16
// Replace/initialize a sample's PCM with mono/stereo 16-bit data and format.
// Allocates/reallocates internal storage as needed.
// Returns 0 on success, -1 on failure.
//
// Parameters:
//   samplenum - index into the mixer sample registry (the same value used by sample_start)
//   pcm       - pointer to interleaved 16-bit PCM
//   frames    - number of frames (samples per channel)
//   freq      - sample rate (e.g., 44100)
//   stereo    - false=mono, true=stereo
// -----------------------------------------------------------------------------
int mixer_upload_sample16(int samplenum,
	const int16_t* pcm,
	uint32_t frames,
	int freq,
	bool stereo)
{
	if (!pcm || frames == 0 || freq <= 0) {
		LOG_ERROR("mixer_upload_sample16: invalid args (pcm=%p frames=%u freq=%d)", (void*)pcm, frames, freq);
		return -1;
	}

	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("mixer_upload_sample16: invalid samplenum %d", samplenum);
		return -1;
	}

	auto& s = lsamples[samplenum];
	if (!s) return -1;

	// (Re)configure format
	s->fx.wFormatTag = WAVE_FORMAT_PCM;
	s->fx.nChannels = stereo ? 2 : 1;
	s->fx.wBitsPerSample = 16;
	s->fx.nSamplesPerSec = freq;
	s->fx.nBlockAlign = static_cast<WORD>(s->fx.nChannels * (s->fx.wBitsPerSample / 8));
	s->fx.nAvgBytesPerSec = s->fx.nSamplesPerSec * s->fx.nBlockAlign;
	s->fx.cbSize = 0;

	const uint32_t channels = s->fx.nChannels;
	s->sampleCount = frames * channels;
	s->dataSize = s->sampleCount * sizeof(int16_t);

	// Allocate/resize storage
	s->data8.reset();
	s->data16 = std::make_unique<int16_t[]>(s->sampleCount);

	// Copy PCM
	std::memcpy(s->data16.get(), pcm, s->dataSize);

	s->buffer = s->data16.get();
	s->state = SoundState::Loaded;
	return 0;
}

// -----------------------------------------------------------------------------
// mixer_init
// Initializes the mixer with an exact (possibly fractional) frames-per-second
// clock and starts the audio worker thread. Audio generation remains frame-locked:
// one call to mixer_update() per emulation/video frame produces and submits that
// frame s audio. Variable samples-per-frame are handled via a 32.32 fixed-point
// accumulator to ensure zero long-term drift.
//
// Parameters:
//   rate - output sample rate in Hz (e.g., 44100 or 48000)
//   fps  - exact emulation/video frame rate (supports fractional,
//          e.g., 60000.0/1001.0 for 59.94, 24000.0/1001.0 for 23.976)
//
// Behavior:
//   - BUFFER_SIZE is kept as a nominal integer size using a rounded FPS; it s
//     used only for logging and initial scratch sizing. The actual per-frame
//     mix length is computed each frame from  fps  and may be N or N+1 samples.
//   - XAudio2 is initialized with the rounded (nominal) FPS for sizing/logging.
// -----------------------------------------------------------------------------
int mixer_init(int rate, int fps)  // <<< integer FPS
{
	// FIX: Ensure any previous instance is fully shut down
	if (audioThreadActive.load(std::memory_order_acquire)) {
		LOG_ERROR("mixer_init: mixer already active, call mixer_end() first");
		return 0;
	}
	
	// Validate inputs.
	if (rate <= 0 || fps <= 0) {
		LOG_ERROR("mixer_init: invalid args (rate=%d fps=%d)", rate, fps);
		return 0;
	}

	// Fixed buffer size for each frame/update.
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;

	if (BUFFER_SIZE <= 0) {
		LOG_ERROR("mixer_init: invalid BUFFER_SIZE=%d (rate=%d fps=%d)", BUFFER_SIZE, rate, fps);
		return 0;
	}

	LOG_INFO("Mixer init, BUFFER SIZE = %d, freq %d framerate %d", BUFFER_SIZE, rate, fps);

	// Stand up the streaming backend (XAudio2 today). Voice path also uses
	// XAudio2 directly, so cache the engine handle here.
	auto backend = std::make_unique<XAudio2Backend>();
	const HRESULT hr = backend->Init(rate, fps);
	if (FAILED(hr)) {
		LOG_ERROR("mixer_init: backend Init failed (hr=0x%08X)", (unsigned)hr);
		return 0; // unique_ptr destructor calls Shutdown
	}
	g_xaudio2 = backend->GetEngine();
	g_backend = std::move(backend);

	// Apply the canonical 80% default through the perceptual curve so g_master_pct
	// and the actual XAudio2 gain agree from the start.
	mixer_set_master_volume(80);

	// Bring up positional audio for the voice path. Failure is non-fatal -
	// streaming + non-positional voices still work; sample_set_world_position
	// will just be a logged no-op.
	g_3d_inited = audio_3d_init(g_backend->OutputChannelMask(),
		g_backend->OutputChannelCount());

	// Reset channel state.
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		channel[i] = CHANNEL();
	}

	sound_paused = false;

	// FIX: Reset ALL thread state flags BEFORE starting the thread
	audioThreadExit.store(false, std::memory_order_release);
	audioThreadRun.store(false, std::memory_order_release);
	queuedFrames.store(0, std::memory_order_release);
	lastSignalTime = std::chrono::steady_clock::now();

	try {
		audioThread = std::thread(audio_thread_func);
		// Mark mixer as active after thread starts successfully.
		audioThreadActive.store(true, std::memory_order_release);
	}
	catch (const std::exception& e) {
		LOG_ERROR("mixer_init: failed to start audio thread: %s", e.what());
		audio_3d_shutdown();
		g_3d_inited = false;
		g_xaudio2 = nullptr;
		g_backend.reset();
		return 0;
	}
	catch (...) {
		LOG_ERROR("mixer_init: failed to start audio thread (unknown exception)");
		audio_3d_shutdown();
		g_3d_inited = false;
		g_xaudio2 = nullptr;
		g_backend.reset();
		return 0;
	}

	return 1;
}

// -----------------------------------------------------------------------------
// mixer_update_internal
// Software mix to an interleaved 16-bit stereo ring buffer for XAudio2.
// - Runs once per emulation/video frame (frame-locked).
// - Supports fractional FPS by mixing a variable number of samples per frame,
//   computed via a 32.32 fixed-point accumulator (g_spf_fp/g_spf_accum).
// - Mono sources: pan is IGNORED (centered). We duplicate to L/R pre-pan.
// - Stereo sources: pan is applied as a post-balance using equal-power gains.
//
// Notes:
//   - Uses per-call variable length: xaudio2_update(soundbuffer, frames*4).
//   - Scratch accumulators auto-grow to the largest frame seen (no shrink),
//     avoiding reallocs on alternating N / N+1 frames.
// -----------------------------------------------------------------------------
static void mixer_update_internal()
{
	if (!g_backend) return;

	BYTE* soundbuffer = g_backend->GetNextBuffer();
	int16_t* out = reinterpret_cast<int16_t*>(soundbuffer);

	std::scoped_lock lock(audioMutex);

	// Fixed number of samples each frame (integer FPS path)
	const int samplesThisFrame = BUFFER_SIZE;
	if (samplesThisFrame <= 0) {
		g_backend->Submit(soundbuffer, 0);
		return;
	}

	// ----- one-time scratch buffers for the per-sample accumulators (no per-call alloc) -----
	static std::unique_ptr<int32_t[]> accumL;
	static std::unique_ptr<int32_t[]> accumR;
	static int scratchSize = 0;
	if (samplesThisFrame > scratchSize) {
		accumL.reset(new int32_t[samplesThisFrame]);
		accumR.reset(new int32_t[samplesThisFrame]);
		scratchSize = samplesThisFrame;
	}

	// ---- mix and track peaks in the same pass ----
	int32_t peak = 0; // max |L| or |R|
	int32_t peakL = 0; // max |L|
	int32_t peakR = 0; // max |R|

	for (int i = 0; i < samplesThisFrame; ++i)
	{
		int32_t fmixL = 0;
		int32_t fmixR = 0;

		if (!sound_paused)
		{
			for (auto it = audio_list.begin(); it != audio_list.end(); )
			{
				int chan = *it;
				auto& ch = channel[chan];

				// Sample lifetime is pinned by CHANNEL::playing_sample (shared_ptr).
				// If the channel was started without a sample, drop it.
				auto& sample = ch.playing_sample;
				if (!sample) {
					it = audio_list.erase(it);
					continue;
				}

				const int chCount   = sample->fx.nChannels;       // 1 or 2
				const int bits      = sample->fx.wBitsPerSample;  // 8 or 16
				const uint32_t totalFrames = sample->sampleCount / static_cast<uint32_t>((std::max)(1, chCount));
				if (totalFrames == 0) { ++it; continue; }

				// End-of-sample / loop-wrap handling at frame granularity.
				// step_q32 == (1<<32) is the native-rate fast path; non-unity step means
				// the channel is doing inline resampling (e.g. 96k chip -> 44.1k system).
				uint32_t idx = static_cast<uint32_t>(ch.pos_q32 >> 32);
				if (idx >= totalFrames) {
					if (!ch.looping) {
						ch.state = SoundState::Stopped;
						ch.isPlaying = false;
						ch.playing_sample.reset();
						it = audio_list.erase(it);
						continue;
					}
					// Wrap: keep fractional remainder, subtract one buffer's worth.
					ch.pos_q32 -= static_cast<uint64_t>(totalFrames) << 32;
					idx = static_cast<uint32_t>(ch.pos_q32 >> 32);
					if (idx >= totalFrames) {
						// Defensive: only triggers if step_q32 is absurdly large.
						ch.pos_q32 = 0;
						idx = 0;
					}
				}

				// Pick the second interp endpoint. For streams the buffer is overwritten
				// every frame by stream_update, so the natural continuation across the
				// buffer boundary (idx == totalFrames - 1) lives in a buffer we don't have
				// yet -- sample-and-hold avoids click without needing 1-frame lookahead.
				// For looping WAV samples the seam IS the loop point, so wrap to frame 0.
				const bool isStream = (ch.stream_type == static_cast<int>(SoundState::Stream));
				uint32_t nidx;
				if (idx + 1 < totalFrames)        nidx = idx + 1;
				else if (isStream)                nidx = idx;
				else if (ch.looping)              nidx = 0;
				else                              nidx = idx;

				const int32_t frac_q15 = static_cast<int32_t>((ch.pos_q32 >> 17) & 0x7FFF);

				// Read the two frames that bracket the fractional position.
				int32_t aL = 0, aR = 0, bL = 0, bR = 0;
				if (bits == 16 && sample->data16) {
					const int16_t* d = sample->data16.get();
					if (chCount == 2) {
						aL = d[idx  * 2 + 0];
						aR = d[idx  * 2 + 1];
						bL = d[nidx * 2 + 0];
						bR = d[nidx * 2 + 1];
					} else {
						aL = aR = d[idx];
						bL = bR = d[nidx];
					}
				}
				else if (bits == 8 && sample->data8) {
					const uint8_t* d = sample->data8.get();
					if (chCount == 2) {
						aL = (static_cast<int32_t>(d[idx  * 2 + 0]) - 128) << 8;
						aR = (static_cast<int32_t>(d[idx  * 2 + 1]) - 128) << 8;
						bL = (static_cast<int32_t>(d[nidx * 2 + 0]) - 128) << 8;
						bR = (static_cast<int32_t>(d[nidx * 2 + 1]) - 128) << 8;
					} else {
						aL = aR = (static_cast<int32_t>(d[idx])  - 128) << 8;
						bL = bR = (static_cast<int32_t>(d[nidx]) - 128) << 8;
					}
				}

				// Linear interpolation in Q15. Provably in int16 range, no saturate needed.
				// When step_q32 == 1<<32, frac_q15 is always 0 and this collapses to sL=aL.
				const int32_t sL = aL + (((bL - aL) * frac_q15) >> 15);
				const int32_t sR = aR + (((bR - aR) * frac_q15) >> 15);

				const float vol = static_cast<float>(ch.vol);
				float gL = 1.0f, gR = 1.0f;
				if (chCount == 2) {
					mixer_pan_gains(ch.pan, gL, gR);
				}

				fmixL += static_cast<int32_t>(sL * vol * gL);
				fmixR += static_cast<int32_t>(sR * vol * gR);

				ch.pos_q32 += ch.step_q32;
				++it;
			}
		}

		accumL[i] = fmixL;
		accumR[i] = fmixR;

		// Track peaks on-the-fly
		const int32_t a = std::abs(fmixL);
		const int32_t b = std::abs(fmixR);
		if (a > peakL) peakL = a;
		if (b > peakR) peakR = b;
		const int32_t p = (a > b) ? a : b;
		if (p > peak) peak = p;
	}

#ifdef USE_VUMETER
	// ---- VU calculation  ----
	constexpr float kInvFullScale = 1.0f / 32767.0f;
	constexpr float kDecay = 0.94f;
	float curL = std::min(1.0f, peakL * kInvFullScale);
	float curR = std::min(1.0f, peakR * kInvFullScale);

	float prevL = g_vuL.load(std::memory_order_relaxed);
	float prevR = g_vuR.load(std::memory_order_relaxed);
	g_vuL.store(vu_decay_step(prevL, curL, kDecay), std::memory_order_relaxed);
	g_vuR.store(vu_decay_step(prevR, curR, kDecay), std::memory_order_relaxed);
#endif

	/*
	// ---- write out with limiter (one pass) ----
	if (peak > INT16_MAX) {
		const float g = 32767.0f / static_cast<float>(peak);
		for (int i = 0; i < samplesThisFrame; ++i) {
			out[2 * i + 0] = static_cast<int16_t>(std::lrintf(accumL[i] * g));
			out[2 * i + 1] = static_cast<int16_t>(std::lrintf(accumR[i] * g));
		}
	}
	else {
		// No limiting needed; clamp just in case
		for (int i = 0; i < samplesThisFrame; ++i) {
			int32_t L = std::clamp(accumL[i], static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
			int32_t R = std::clamp(accumR[i], static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));
			out[2 * i + 0] = static_cast<int16_t>(L);
			out[2 * i + 1] = static_cast<int16_t>(R);
		}
	}
	*/
	// ---- write out with Soft Clipping (Per-Sample Saturation) ----

	// We process every sample individually. This prevents the "pumping" 
	// effect where a loud sound momentarily drops the volume of the entire frame.

	for (int i = 0; i < samplesThisFrame; ++i) {
		// 1. Get raw accumulated values (potentially much larger than 32767)
		float mixL = static_cast<float>(accumL[i]);
		float mixR = static_cast<float>(accumR[i]);

		// --- OPTION 1: True Soft Clipping (Analog Style) ---
		// Normalize to approx -1.0 to 1.0, apply tanh curve, scale back.
		// This rounds off peaks smoothly. 
		// Note: std::tanh is fast enough for audio on modern CPUs.

		float normL = std::tanh(mixL / 32768.0f);
		float normR = std::tanh(mixR / 32768.0f);

		out[2 * i + 0] = static_cast<int16_t>(normL * 32767.0f);
		out[2 * i + 1] = static_cast<int16_t>(normR * 32767.0f);


		// --- OPTION 2: Hard Clamping (Crisp / Retro Standard) ---
		// If you find Option 1 sounds too "muddy" or "quiet" for 8-bit games,
		// comment out Option 1 and uncomment this block instead.
		/*
		int32_t L = std::clamp(accumL[i], -32768, 32767);
		int32_t R = std::clamp(accumR[i], -32768, 32767);
		out[2 * i + 0] = static_cast<int16_t>(L);
		out[2 * i + 1] = static_cast<int16_t>(R);
		*/
	}
		
	// Submit variable-length frame (stereo 16-bit = 4 bytes per frame)
	g_backend->Submit(soundbuffer, static_cast<DWORD>(samplesThisFrame * 4));
}

// -----------------------------------------------------------------------------
// mixer_reap_voice_channels
// Walks the voice-path channels and releases any whose XAudio2 buffers have
// drained. The voice object itself is kept (cheap to reuse on the next
// sample_start) but the SAMPLE reference is released so that, if the registry
// slot was nulled by sample_remove, the PCM memory is freed promptly.
//
// Also refreshes the X3DAudio output matrix for any channel marked positional,
// so the spatial placement tracks listener and source motion automatically.
//
// Called once per frame from mixer_update.
// -----------------------------------------------------------------------------
static void mixer_reap_voice_channels()
{
	std::scoped_lock lock(audioMutex);
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		auto& ch = channel[i];
		if (!ch.voice || !ch.playing_sample) continue;

		XAUDIO2_VOICE_STATE st{};
		ch.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (st.BuffersQueued == 0) {
			ch.isPlaying = false;
			ch.state = SoundState::Stopped;
			ch.is_positional = false;       // sample is done; positional state goes with it
			ch.playing_sample.reset();
			continue;
		}

		// Still playing - refresh positional matrix if applicable.
		if (ch.is_positional && g_3d_inited) {
			audio_3d_apply_2d(ch.voice, ch.world_x, ch.world_y,
				ch.playing_sample->fx.nChannels);
		}
	}
}

// -----------------------------------------------------------------------------
// mixer_update
// Reaps any drained voice-path channels (so removed SAMPLEs can be freed),
// then signals the audio thread to run mixer_update_internal().
// -----------------------------------------------------------------------------
void mixer_update()
{
	// FIX: Only signal if mixer is active
	if (!audioThreadActive.load(std::memory_order_acquire)) {
		return;
	}

	mixer_reap_voice_channels();

	queuedFrames++;
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		lastSignalTime = std::chrono::steady_clock::now(); // read by audio thread under the same lock
		audioThreadRun.store(true, std::memory_order_release);
	}
	audioCV.notify_one();
}

void mixer_end()
{
	// FIX: Check if mixer is even active
	if (!audioThreadActive.load(std::memory_order_acquire)) {
		LOG_INFO("mixer_end: mixer not active, nothing to do");
		return;
	}
	
	LOG_INFO("mixer_end: shutting down audio thread...");
	
	// FIX: Signal the thread to exit
	{
		std::lock_guard<std::mutex> lock(audioCVMutex);
		audioThreadExit.store(true, std::memory_order_release);
	}
	audioCV.notify_one();
	
	// FIX: Wait for thread to actually finish
	if (audioThread.joinable()) {
		audioThread.join();
	}
	
	// FIX: Mark mixer as inactive AFTER thread has joined
	audioThreadActive.store(false, std::memory_order_release);
	
	LOG_INFO("mixer_end: audio thread stopped, cleaning up resources...");

	// Tear down positional first so any matrix work in flight stops touching
	// voices we're about to destroy.
	audio_3d_shutdown();
	g_3d_inited = false;

	// Stop all channels under the mutex BEFORE destroying the backend. When
	// the IXAudio2 engine goes away every IXAudio2SourceVoice* it owns becomes
	// invalid; if we left them parked in CHANNEL::voice, every subsequent
	// sample_set_volume / sample_set_pan / sample_set_freq / sample_playing
	// call (between mixer_end and the next mixer_init) would deref freed
	// memory. stop_channel_locked destroys each voice cleanly, clears
	// ch.voice and ch.playing_sample, and removes the channel from
	// audio_list -- leaving the channel array in the same default-constructed
	// state that mixer_init expects.
	{
		std::scoped_lock lock(audioMutex);
		for (int i = 0; i < MAX_CHANNELS; ++i) {
			stop_channel_locked(i);
		}
		audio_list.clear();
		for (auto& sample : lsamples) {
			if (sample && sample->buffer)
				LOG_INFO("Freeing sample #%d named %s", sample->num, sample->name.c_str());
		}
		lsamples.clear();
	}

	// Tear down the backend. Voice path's cached engine handle goes invalid
	// at the same moment - clear it before reset() so any racing caller fails fast.
	g_xaudio2 = nullptr;
	g_backend.reset();

	// Reset sound_id so new samples start fresh on next init.
	sound_id = -1;
	
	LOG_INFO("mixer_end: complete");
}

// -----------------------------------------------------------------------------
// stop_channel_locked
// Path-agnostic full stop of a single channel. Caller must already hold
// audioMutex and have validated chanid in [0, MAX_CHANNELS).
//
// Destroys any XAudio2 voice on the channel, evicts the channel from the
// software-mixer audio_list, resets all playback/position/rate/positional
// bookkeeping, and releases the SAMPLE reference so sample_remove can free
// memory. Used by sample_stop, sample_stop_mixer, stream_stop, and
// samples_stop_all -- they all want the same cleanup regardless of which
// start API the channel was originally launched with.
// -----------------------------------------------------------------------------
static void stop_channel_locked(int chanid)
{
	auto& ch = channel[chanid];

	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}
	audio_list.remove(chanid);

	ch.state             = SoundState::Stopped;
	ch.isPlaying         = false;
	ch.looping           = 0;
	ch.pos_q32           = 0;
	ch.step_q32          = (1ull << 32);
	ch.stream_type       = 0;
	ch.is_positional     = false;
	ch.world_x = ch.world_y = 0.0f;
	ch.loaded_sample_num = -1;
	ch.playing_sample.reset();
}

void sample_stop(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_stop: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);
	stop_channel_locked(chanid);
}

// -----------------------------------------------------------------------------
// SetPan
// Applies panning/balance on a source voice's output matrix.
//
// Builds a srcCh x dstCh matrix where dstCh is the master's actual channel
// count (possibly surround). Audio is routed to the front L/R only; surround
// channels stay silent for non-positional voices. Positional voices skip this
// path entirely and use X3DAudio's matrix via audio_3d_apply_2d.
//
// Behavior:
//   - Mono source: pan is ignored; output is centered (L=R=1.0, others 0).
//   - Stereo source: equal-power balance to front L / front R.
// -----------------------------------------------------------------------------
static void SetPan(IXAudio2SourceVoice* voice, int panByte)
{
	if (!voice) return;

	XAUDIO2_VOICE_DETAILS details{};
	voice->GetVoiceDetails(&details);
	const UINT32 srcCh = details.InputChannels;
	const UINT32 dstCh = g_backend ? g_backend->OutputChannelCount() : 2;
	if (dstCh == 0 || srcCh == 0) return;

	// XAudio2 SetOutputMatrix layout (per official docs): the level sent from
	// source channel S to destination channel D lives at
	//   pLevelMatrix[SourceChannels * D + S]
	// i.e. the matrix is laid out destination-major (one row per destination
	// channel, columns are source channels). This is the OPPOSITE of what the
	// "row-major rows=source" reading would suggest; getting it wrong puts
	// stereo R onto the LFE channel and produces silence with no error.
	std::vector<float> m(srcCh * dstCh, 0.0f);

	if (srcCh == 1) {
		// Mono -> front L + front R (center, pan ignored).
		// For srcCh=1 the formula collapses to m[D], so this works regardless
		// of which interpretation you assumed.
		m[srcCh * 0 + 0] = 1.0f;                  // src 0 -> dst 0 (FL)
		if (dstCh >= 2) m[srcCh * 1 + 0] = 1.0f;  // src 0 -> dst 1 (FR)
	}
	else if (srcCh == 2 && dstCh >= 2) {
		// Stereo balance to front L / front R using equal-power gains.
		float gL = 1.0f, gR = 1.0f;
		mixer_pan_gains(panByte, gL, gR);
		m[srcCh * 0 + 0] = gL; // src 0 (L) -> dst 0 (FL)
		m[srcCh * 1 + 1] = gR; // src 1 (R) -> dst 1 (FR)
	}
	else {
		// Unusual source layout: identity over min(src, dst), rest silent.
		const UINT32 minCh = (srcCh < dstCh) ? srcCh : dstCh;
		for (UINT32 c = 0; c < minCh; ++c) {
			m[srcCh * c + c] = 1.0f;
		}
	}

	voice->SetOutputMatrix(nullptr, srcCh, dstCh, m.data());
}

// -----------------------------------------------------------------------------
// sample_remove
// Removes a sample from the registry. The slot is nulled, NOT erased, so the
// num == index invariant that the rest of the code relies on is preserved.
// If any channel is still playing this sample, its CHANNEL::playing_sample
// shared_ptr keeps the SAMPLE memory alive until the channel stops. The audio
// thread reads through ch.playing_sample (not the registry), so playback in
// progress continues uninterrupted.
// -----------------------------------------------------------------------------
void sample_remove(int samplenum)
{
	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("sample_remove: invalid samplenum %d", samplenum);
		return;
	}
	auto& slot = lsamples[samplenum];
	if (!slot) return; // already removed

	const long uses = slot.use_count();
	LOG_INFO("sample_remove: dropping sample #%d '%s' (registry ref count was %ld; channels still playing keep their reference)",
		samplenum, slot->name.c_str(), uses);

	slot.reset(); // null the slot; channels with playing_sample stay valid
}

// -----------------------------------------------------------------------------
// sample_get_position
// Returns playback position in FRAMES (per-channel sample ticks), not bytes
// and not interleaved sample elements. 0 if the channel has no active sample.
// -----------------------------------------------------------------------------
int sample_get_position(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 0;
	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];
	if (!ch.playing_sample) return 0;

	return static_cast<int>(ch.pos_q32 >> 32);
}

// -----------------------------------------------------------------------------
// sample_set_volume / sample_get_volume         (0..255, legacy)
// sample_set_volume_percent / sample_get_volume_percent  (0..100)
//
// Both APIs route through the same canonical perceptual curve
// (VolumeNormalizedToLinear in mixer.h), so equivalent inputs produce the same
// gain. The stored byte (ch.volume) is the source of truth; ch.vol holds the
// derived linear gain used by both the XAudio2 voice path (SetVolume) and the
// software-mixer per-sample multiply.
// -----------------------------------------------------------------------------
void sample_set_volume(int chanid, int volume)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_volume: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];
	volume = std::clamp(volume, 0, 255);
	ch.volume = volume;

	const float gain = VolumeByteToLinear(volume);
	ch.vol = static_cast<double>(gain);

	if (ch.voice) {
		ch.voice->SetVolume(gain);
	}
}

int sample_get_volume(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 0;
	std::scoped_lock lock(audioMutex);
	return std::clamp(channel[chanid].volume, 0, 255);
}

void sample_set_volume_percent(int chanid, int pct)
{
	sample_set_volume(chanid, VolPercentToByte(pct));
}

int sample_get_volume_percent(int chanid)
{
	return VolByteToPercent(sample_get_volume(chanid));
}

// -----------------------------------------------------------------------------
// sample_set_position
// Seek a channel to pos_frames. Software-mixer path: clamped to sample length
// and written into pos_q32 (the 32.32 fixed-point position used by the mix loop).
// Voice path: XAudio2 has no SetPosition; seeking requires Stop+FlushSourceBuffers
// +resubmit-with-PlayBegin which we don't do today, so the voice path is a logged
// no-op. Tell me if you need it.
// -----------------------------------------------------------------------------
void sample_set_position(int chanid, int pos_frames)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_position: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	if (ch.voice) {
		LOG_ERROR("sample_set_position: channel %d is on the voice path; seek not implemented", chanid);
		return;
	}
	if (!ch.playing_sample) {
		LOG_ERROR("sample_set_position: channel %d has no active sample", chanid);
		return;
	}

	const int nch = std::max<int>(1, ch.playing_sample->fx.nChannels);
	const uint32_t total_frames = ch.playing_sample->sampleCount / static_cast<uint32_t>(nch);
	if (pos_frames < 0) pos_frames = 0;
	if (static_cast<uint32_t>(pos_frames) >= total_frames) pos_frames = static_cast<int>(total_frames > 0 ? total_frames - 1 : 0);

	ch.pos_q32 = static_cast<uint64_t>(pos_frames) << 32;
}

int sample_get_freq(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 0;
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	// Read native rate from the SAMPLE so the answer is correct on both the
	// voice path (where ch.frequency mirrors this at start time) and the
	// mixer/stream path (where ch.frequency is never set). Returns the BASE
	// rate, not the currently-applied playback rate -- see header note.
	if (!ch.playing_sample) return 0;
	return static_cast<int>(ch.playing_sample->fx.nSamplesPerSec);
}

void sample_set_freq(int chanid, int freq)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS || freq <= 0) return;
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.voice) {
		if (ch.frequency > 0) {
			float ratio = static_cast<float>(freq) / static_cast<float>(ch.frequency);
			ch.voice->SetFrequencyRatio(ratio);
		}
	} else if (SYS_FREQ > 0) {
		// Mixer path: step is "source frames per output frame". Setting freq = F
		// means "consume F source frames per second of output", so the mix loop
		// resamples F -> SYS_FREQ inline.
		ch.step_q32 = (static_cast<uint64_t>(freq) << 32) / static_cast<uint64_t>(SYS_FREQ);
	}
}

void sample_set_pan(int chanid, int pan)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_pan: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	ch.pan = std::clamp(pan, 0, 255);
	if (ch.voice) {
		SetPan(ch.voice, ch.pan);
	}
}

int sample_get_pan(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return 128;
	std::scoped_lock lock(audioMutex);
	return channel[chanid].pan;
}

// -----------------------------------------------------------------------------
// Positional audio (2D, camera-locked listener)
// -----------------------------------------------------------------------------
void mixer_set_listener_2d(float x, float y)
{
	std::scoped_lock lock(audioMutex);
	g_listener_x = x;
	g_listener_y = y;
	if (g_3d_inited) audio_3d_set_listener_2d(x, y);
}

void sample_set_world_position(int chanid, float x, float y)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_set_world_position: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];
	if (!ch.voice) {
		LOG_ERROR("sample_set_world_position: channel %d is not on the voice path", chanid);
		return;
	}

	ch.is_positional = true;
	ch.world_x = x;
	ch.world_y = y;

	// Apply immediately so the first frame is already spatialized; the per-
	// frame update keeps it current as the listener or source moves later.
	if (g_3d_inited && ch.playing_sample) {
		audio_3d_apply_2d(ch.voice, x, y, ch.playing_sample->fx.nChannels);
	}
}

void sample_clear_world_position(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) return;
	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];
	ch.is_positional = false;
	ch.world_x = ch.world_y = 0.0f;

	// Restore the channel's stored pan via SetOutputMatrix.
	if (ch.voice) {
		SetPan(ch.voice, ch.pan);
	}
}

void sample_start(int chanid, int samplenum, int loop)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_start: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum] == nullptr ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	// Check the XAudio2 backend BEFORE tearing down any existing voice on this
	// channel -- otherwise a post-shutdown call would leave the channel with
	// a destroyed voice and no replacement.
	if (!g_xaudio2) {
		LOG_ERROR("sample_start: voice path unavailable (no XAudio2 backend)");
		return;
	}

	auto& ch = channel[chanid];

	// If this channel was previously running on the software-mixer / stream
	// path (sample_start_mixer or stream_start) and the caller didn't call
	// the matching stop, evict it from the mix list and clear mixer-path
	// state -- otherwise the mix loop would keep mixing the channel via the
	// new sample's data while XAudio2 also plays it, doubling output and
	// corrupting state.
	audio_list.remove(chanid);
	ch.stream_type = 0;
	ch.pos_q32     = 0;
	ch.step_q32    = (1ull << 32);

	// If there is an existing voice on this channel, stop and destroy it.
	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}

	auto& sample = lsamples[samplenum];

	// The 8.0f is really important here and required for the StarCastle drone.
	if (FAILED(g_xaudio2->CreateSourceVoice(&ch.voice, &sample->fx, 0, 8.0f)))
	{
		LOG_ERROR("Failed to create voice for sample %d", sample->num);
		// We already tore down the previous voice; leave the channel in a
		// clean stopped state so the next sample_playing call doesn't lie.
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.playing_sample.reset();
		return;
	}

	// Initialize channel state.
	ch.isAllocated = true;
	ch.isReleased = false;
	ch.isPlaying = true;
	ch.state = SoundState::Playing;   // Important: keep state consistent for voice channels
	ch.looping = loop;
	ch.volume = 255;
	ch.pan = 128;
	ch.frequency = sample->fx.nSamplesPerSec;
	ch.loaded_sample_num = samplenum;
	ch.playing_sample = sample;       // pin SAMPLE memory for XAudio2's buffer reads
	ch.is_positional = false;         // fresh start; caller can opt in via sample_set_world_position
	ch.world_x = ch.world_y = 0.0f;

	// Build and submit the XAudio2 buffer.
	std::memset(&ch.buffer, 0, sizeof(ch.buffer));
	ch.buffer.AudioBytes = sample->dataSize;
	ch.buffer.pAudioData = static_cast<BYTE*>(sample->buffer);
	ch.buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

	if (FAILED(ch.voice->SubmitSourceBuffer(&ch.buffer))) {
		LOG_ERROR("sample_start: SubmitSourceBuffer failed (sample=%d chan=%d)", samplenum, chanid);
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.playing_sample.reset();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
		return;
	}

	// We have to set the volume manually here to avoid a scoped_lock recursive error.
	const float gain = VolumeByteToLinear(ch.volume);
	ch.vol = gain;
	ch.voice->SetVolume(gain);

	SetPan(ch.voice, ch.pan);

	// Start() can fail (rare, but possible on device reset, etc.). If it does,
	// don't leave the channel claiming to be playing -- the buffer is still
	// queued so BuffersQueued > 0 and sample_playing would return 1 forever.
	if (FAILED(ch.voice->Start())) {
		LOG_ERROR("sample_start: Start failed (sample=%d chan=%d)", samplenum, chanid);
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.playing_sample.reset();
	}
}

int sample_playing(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_playing: invalid channel %d", chanid);
		return 0;
	}

	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];

	// Voice-path fix-up: if the XAudio2 queue has drained, the one-shot is
	// finished. Update bookkeeping here so queue-based callers (Sega speech,
	// etc.) don't get stuck waiting on an already-completed voice. The reap
	// loop in mixer_update does the same fix-up; this is the defensive path
	// for callers that poll sample_playing without driving mixer_update.
	if (ch.voice) {
		XAUDIO2_VOICE_STATE st{};
		ch.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (st.BuffersQueued == 0) {
			ch.isPlaying = false;
			ch.state = SoundState::Stopped;
			// XAudio2 is no longer reading the buffer; release our reference
			// so sample_remove can actually free memory if the slot was nulled.
			ch.playing_sample.reset();
		} else {
			ch.isPlaying = true;
			ch.state = SoundState::Playing;
		}
	}

	// ch.state is the canonical playback flag on both voice and mixer paths
	// (sample_start, sample_start_mixer, stream_start all set Playing; the
	// mix loop and end-of-sample logic transition to Stopped). ch.isPlaying
	// is only maintained on the voice path, so reading state is the path-
	// agnostic answer.
	return (ch.state == SoundState::Playing) ? 1 : 0;
}


void sample_end(int chanid)
{
	// Validate channel index against your fixed array
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_end: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	// Stop looping for both paths.
	ch.looping = 0;

	// If this channel is using an XAudio2 voice and was looping, exit the loop.
	// Note: ExitLoop affects only buffers submitted with XAUDIO2_LOOP_INFINITE.
	if (ch.voice) {
		ch.voice->ExitLoop();
	}
}

void sample_start_mixer(int chanid, int samplenum, int loop)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_start_mixer: invalid channel %d", chanid);
		return;
	}

	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		!lsamples[samplenum] ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	auto& ch = channel[chanid];

	// Cross-path cleanup, symmetric with sample_start: tear down any prior
	// voice on this channel so it doesn't linger as an orphan responding to
	// volume/pan/freq updates, and evict any prior mix-list membership so the
	// push_back below doesn't create a duplicate.
	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}
	audio_list.remove(chanid);

	ch.state = SoundState::Playing;
	ch.stream_type = static_cast<int>(SoundState::PCM);
	ch.loaded_sample_num = samplenum;
	ch.playing_sample = lsamples[samplenum]; // pin lifetime; survives sample_remove
	ch.looping = loop;
	ch.pos_q32 = 0;
	// step_q32 reflects sample-native-rate -> output-rate. For samples that were
	// resampled to SYS_FREQ at load (the default), this is exactly 1<<32 and the
	// mix loop's interp collapses to plain reads.
	const uint32_t native = ch.playing_sample->fx.nSamplesPerSec;
	ch.step_q32 = (native > 0 && SYS_FREQ > 0)
		? (static_cast<uint64_t>(native) << 32) / static_cast<uint64_t>(SYS_FREQ)
		: (1ull << 32);
	// Voice-path positional state doesn't apply to the mixer path; clear it so
	// a later transition back to voice path starts from a clean baseline.
	ch.is_positional = false;
	ch.world_x = ch.world_y = 0.0f;

	audio_list.push_back(chanid);
	LOG_INFO("Playing Sample #%d :%s", samplenum, ch.playing_sample->name.c_str());
}

void sample_end_mixer(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_end_mixer: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);
	channel[chanid].looping = 0;
}

void sample_stop_mixer(int chanid)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("sample_stop_mixer: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);
	stop_channel_locked(chanid);
}

// Legacy alias - the previous implementation incorrectly fed a 0..255 value
// into a 0..100 function. Now identical to sample_set_volume so both paths
// use the same perceptual curve.
void sample_set_volume_mixer(int chanid, int volume255)
{
	sample_set_volume(chanid, volume255);
}

// Start a stream on the given channel. bits must be 8 or 16; frame_rate > 0;
// stereo selects mono (false) or interleaved stereo (true) buffer layout.
void stream_start(int chanid, int /*stream*/, int bits, int frame_rate, bool stereo)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("stream_start: invalid channel %d", chanid);
		return;
	}
	if (frame_rate <= 0) {
		LOG_ERROR("stream_start: invalid frame_rate %d (must be > 0)", frame_rate);
		return;
	}
	if (bits != 8 && bits != 16) {
		LOG_ERROR("stream_start: unsupported bits=%d (only 8 or 16 supported)", bits);
		return;
	}

	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];

	// Cross-path cleanup, symmetric with sample_start / sample_start_mixer:
	// tear down any prior voice so it doesn't linger as an orphan responding
	// to volume/pan/freq updates, and evict any prior mix-list membership so
	// the push_back below doesn't create a duplicate.
	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}
	audio_list.remove(chanid);

	// Build the per-frame stream SAMPLE inline -- create_sample takes its own
	// lock so we can't call it from inside this scope without re-entering the
	// mutex. The SAMPLE owns the buffer that stream_update writes into each
	// frame; the channel pins its lifetime via playing_sample.
	auto sample = std::make_shared<SAMPLE>();
	sample->num  = ++sound_id;
	sample->name = "STREAM" + std::to_string(sample->num);

	LOG_INFO("Creating Audio Sample with name %s and sound id %d", sample->name.c_str(), sample->num);

	sample->fx.wFormatTag     = WAVE_FORMAT_PCM;
	sample->fx.nChannels      = stereo ? 2 : 1;
	sample->fx.nSamplesPerSec = SYS_FREQ;
	sample->fx.wBitsPerSample = static_cast<WORD>(bits);
	sample->fx.nBlockAlign    = static_cast<WORD>(sample->fx.nChannels * (bits / 8));
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->fx.cbSize         = 0;
	sample->state             = SoundState::Loaded;

	const int len = SYS_FREQ / frame_rate;
	const uint32_t channels = sample->fx.nChannels;
	sample->sampleCount = static_cast<uint32_t>(len) * channels;
	sample->dataSize    = sample->sampleCount * (bits / 8);

	if (bits == 8) {
		sample->data8 = std::make_unique<uint8_t[]>(sample->dataSize);
		std::memset(sample->data8.get(), 0, sample->dataSize);
		sample->buffer = sample->data8.get();
	} else {
		sample->data16 = std::make_unique<int16_t[]>(sample->sampleCount);
		std::memset(sample->data16.get(), 0, sample->dataSize);
		sample->buffer = sample->data16.get();
	}

	lsamples.push_back(sample);
	ch.playing_sample = sample;

	ch.state             = SoundState::Playing;
	ch.loaded_sample_num = sample->num;
	ch.looping           = 1;
	ch.pos_q32           = 0;
	// Default step: buffer is at SYS_FREQ, mixer reads at SYS_FREQ -> no rate conversion.
	// Call stream_set_native_rate to override (e.g. for a 96 kHz chip emulator).
	ch.step_q32          = (1ull << 32);
	ch.stream_type       = static_cast<int>(SoundState::Stream);
	// Voice-path positional state doesn't apply to streams; clear it so a
	// later transition back to voice path starts from a clean baseline.
	ch.is_positional     = false;
	ch.world_x = ch.world_y = 0.0f;

	audio_list.push_back(chanid);
}

// Create a mono stream:
void stream_start(int chanid, int stream, int bits, int frame_rate)
{
	LOG_INFO("Starting a mono stream");
	stream_start(chanid, stream, bits, frame_rate, /*stereo=*/false);
}

void stream_stop(int chanid, int /*stream*/)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS) {
		LOG_ERROR("stream_stop: invalid channel %d", chanid);
		return;
	}
	std::scoped_lock lock(audioMutex);
	stop_channel_locked(chanid);
}

void stream_update(int chanid, short* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing && 
	    ch.loaded_sample_num >= 0 && 
	    ch.loaded_sample_num < static_cast<int>(lsamples.size())) {
		auto& sample = lsamples[ch.loaded_sample_num];
		if (sample && sample->data16) {
			std::memcpy(sample->data16.get(), data, sample->dataSize);
		}
	}
}

void stream_update(int chanid, unsigned char* data)
{
	std::scoped_lock lock(audioMutex);
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing &&
	    ch.loaded_sample_num >= 0 &&
	    ch.loaded_sample_num < static_cast<int>(lsamples.size())) {
		auto& sample = lsamples[ch.loaded_sample_num];
		if (sample && sample->data8) {
			std::memcpy(sample->data8.get(), data, sample->dataSize);
		}
	}
}

// -----------------------------------------------------------------------------
// stream_set_native_rate
// Resize a stream's buffer to native_rate frames-per-second (instead of SYS_FREQ)
// and set the channel's resampling step so the mix loop does source->output rate
// conversion inline. New buffer holds the same duration (1 / fps seconds) as
// before, just at the new rate.
//
// After this call, the chip emulator should write native_rate / fps frames per
// stream_update, and the mixer will interpolate them to SYS_FREQ on the fly.
// -----------------------------------------------------------------------------
void stream_set_native_rate(int chanid, int native_rate)
{
	if (chanid < 0 || chanid >= MAX_CHANNELS || native_rate <= 0 || SYS_FREQ <= 0) {
		LOG_ERROR("stream_set_native_rate: invalid args (chanid=%d, rate=%d)", chanid, native_rate);
		return;
	}
	std::scoped_lock lock(audioMutex);

	auto& ch = channel[chanid];
	if (ch.stream_type != static_cast<int>(SoundState::Stream) || !ch.playing_sample) {
		LOG_ERROR("stream_set_native_rate: channel %d is not a stream", chanid);
		return;
	}

	auto& sample = ch.playing_sample;
	const uint32_t channels = std::max<uint32_t>(1, sample->fx.nChannels);
	const uint32_t old_rate = std::max<uint32_t>(1, sample->fx.nSamplesPerSec);
	const uint32_t old_frames = sample->sampleCount / channels;

	// Preserve duration: new_frames / native_rate == old_frames / old_rate.
	uint32_t new_frames = static_cast<uint32_t>(
		(static_cast<uint64_t>(old_frames) * static_cast<uint64_t>(native_rate)) / old_rate);
	if (new_frames == 0) new_frames = 1;

	const uint32_t bits = sample->fx.wBitsPerSample;
	const uint32_t new_sample_count = new_frames * channels;
	const uint32_t new_data_size = new_sample_count * (bits / 8);

	// Reallocate the PCM buffer at the new size.
	if (bits == 8) {
		sample->data8 = std::make_unique<uint8_t[]>(new_data_size);
		std::memset(sample->data8.get(), 0, new_data_size);
		sample->buffer = sample->data8.get();
		sample->data16.reset();
	} else {
		sample->data16 = std::make_unique<int16_t[]>(new_sample_count);
		std::memset(sample->data16.get(), 0, new_data_size);
		sample->buffer = sample->data16.get();
		sample->data8.reset();
	}

	sample->fx.nSamplesPerSec  = static_cast<DWORD>(native_rate);
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->sampleCount = new_sample_count;
	sample->dataSize    = new_data_size;

	// Reset position and set the resampling step (source frames per output frame).
	ch.pos_q32  = 0;
	ch.step_q32 = (static_cast<uint64_t>(native_rate) << 32) / static_cast<uint64_t>(SYS_FREQ);

	LOG_INFO("stream_set_native_rate: chan %d -> %d Hz, buffer %u frames (step_q32=0x%llX)",
		chanid, native_rate, new_frames, static_cast<unsigned long long>(ch.step_q32));
}

void restore_audio()
{
	mixer_set_master_volume(last_master_pct);
	sound_paused = false;
}

void pause_audio()
{
	last_master_pct = g_master_pct;
	mixer_set_master_volume(0);
	sound_paused = true;
#ifdef USE_VUMETER
	mixer_reset_vu();
#endif
}

// -----------------------------------------------------------------------------
// create_sample
// Allocates an empty PCM sample buffer.
// - sampleCount is stored as "elements" (samples), i.e., frames * channels.
// - dataSize is bytes = sampleCount * (bits/8).
// -----------------------------------------------------------------------------
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name)
{
	// Minimal input guards
	if (freq <= 0 || len <= 0) {
		LOG_ERROR("create_sample: invalid freq=%d or len=%d", freq, len);
		return -1;
	}

	// Normalize bits (support 8 or 16); fallback to 16-bit on odd inputs
	if (bits != 8 && bits != 16) {
		LOG_ERROR("create_sample: unsupported bits=%d, defaulting to 16-bit", bits);
		bits = 16;
	}

	auto sample = std::make_shared<SAMPLE>();
	sample->num = ++sound_id;
	sample->name = (name == "STREAM") ? name + std::to_string(sample->num) : name;

	LOG_INFO("Creating Audio Sample with name %s and sound id %d", sample->name.c_str(), sample->num);

	sample->fx.wFormatTag = WAVE_FORMAT_PCM;
	sample->fx.nChannels = is_stereo ? 2 : 1;
	sample->fx.nSamplesPerSec = freq;
	sample->fx.wBitsPerSample = static_cast<WORD>(bits);
	sample->fx.nBlockAlign = static_cast<WORD>(sample->fx.nChannels * (bits / 8));
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->fx.cbSize = 0; // PCM baseline
	sample->state = SoundState::Loaded;

	// len is in FRAMES; store "elements" (interleaved samples)
	const uint32_t channels = sample->fx.nChannels;
	sample->sampleCount = static_cast<uint32_t>(len) * channels;
	sample->dataSize = sample->sampleCount * (bits / 8);

	if (bits == 8) {
		sample->data8 = std::make_unique<uint8_t[]>(sample->dataSize);
		std::memset(sample->data8.get(), 0, sample->dataSize);
		sample->buffer = sample->data8.get();
	}
	else { // 16-bit
		sample->data16 = std::make_unique<int16_t[]>(sample->sampleCount);
		std::memset(sample->data16.get(), 0, sample->dataSize);
		sample->buffer = sample->data16.get();
	}

	std::scoped_lock lock(audioMutex);
	lsamples.push_back(sample);
	return sample->num;
}

std::string numToName(int num)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample && sample->num == num) {
			return sample->name;
		}
	}
	LOG_ERROR("Name not found for Sample #%d!", num);
	return "";
}

int nameToNum(const std::string& name)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample && sample->name == name) {
			return sample->num;
		}
	}
	return -1;
}

// Now that sample_remove can null slots, num and index are identical (the
// invariant is preserved by never erasing). This is kept for API compat.
int snumlookup(int snum)
{
	std::scoped_lock lock(audioMutex);
	if (snum < 0 || snum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("Sample number not found in lookup: %d", snum);
		return -1;
	}
	if (!lsamples[snum]) {
		LOG_ERROR("Sample number %d has been removed", snum);
		return -1;
	}
	return snum;
}

// -----------------------------------------------------------------------------
// mixer_alloc_channel
// Find the lowest free channel in [low, high). "Free" = no XAudio2 voice, no
// pinned playing_sample, not currently in the software mix list. The mutex
// protects the lookup against concurrent state changes, but the gap between
// returning the channel index and the caller's stream_start / sample_start
// is unsynchronized -- callers should allocate from one thread.
// -----------------------------------------------------------------------------
int mixer_alloc_channel(int low, int high)
{
	if (low < 0) low = 0;
	if (high > MAX_CHANNELS) high = MAX_CHANNELS;
	if (low >= high) return -1;

	std::scoped_lock lock(audioMutex);
	for (int i = low; i < high; ++i) {
		const auto& ch = channel[i];
		if (ch.voice) continue;          // voice-path slot in use
		if (ch.playing_sample) continue; // mixer-path slot in use
		// Defensive: also exclude anything that's currently in the mix list.
		if (std::find(audio_list.begin(), audio_list.end(), i) != audio_list.end())
			continue;
		return i;
	}
	LOG_ERROR("mixer_alloc_channel: no free channel in [%d, %d)", low, high);
	return -1;
}

// -----------------------------------------------------------------------------
// save_sample_to_buffer
// Export a loaded sample to a WAV file format buffer.
// The caller is responsible for writing the buffer to disk using their
// own file I/O utilities.
//
// Parameters:
//   samplenum  - sample ID to export
//   out_buffer - output vector that will receive the WAV file data
//
// Returns:
//   true on success, false on failure
// -----------------------------------------------------------------------------
bool save_sample_to_buffer(int samplenum, std::vector<uint8_t>& out_buffer)
{
	std::scoped_lock lock(audioMutex);
	
	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) {
		LOG_ERROR("save_sample_to_buffer: invalid samplenum %d", samplenum);
		return false;
	}

	const auto& sample = lsamples[samplenum];
	if (!sample) {
		LOG_ERROR("save_sample_to_buffer: sample %d is null", samplenum);
		return false;
	}

	// Calculate sizes
	uint32_t subchunk1Size = 16;
	uint32_t subchunk2Size = static_cast<uint32_t>(sample->dataSize);
	uint32_t chunkSize = 4 + (8 + subchunk1Size) + (8 + subchunk2Size);
	
	// Total file size: RIFF header (8) + chunkSize
	size_t totalSize = 8 + chunkSize;
	out_buffer.clear();
	out_buffer.reserve(totalSize);
	
	// Helper lambda to append data to buffer
	auto append = [&out_buffer](const void* data, size_t size) {
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		out_buffer.insert(out_buffer.end(), bytes, bytes + size);
	};
	
	// Write RIFF header
	append("RIFF", 4);
	append(&chunkSize, 4);
	append("WAVE", 4);
	
	// Write fmt chunk
	append("fmt ", 4);
	append(&subchunk1Size, 4);
	append(&sample->fx.wFormatTag, 2);
	append(&sample->fx.nChannels, 2);
	append(&sample->fx.nSamplesPerSec, 4);
	append(&sample->fx.nAvgBytesPerSec, 4);
	append(&sample->fx.nBlockAlign, 2);
	append(&sample->fx.wBitsPerSample, 2);
	
	// Write data chunk
	append("data", 4);
	append(&subchunk2Size, 4);
	
	if (sample->fx.wBitsPerSample == 8 && sample->data8) {
		append(sample->data8.get(), sample->dataSize);
	}
	else if (sample->fx.wBitsPerSample == 16 && sample->data16) {
		append(sample->data16.get(), sample->dataSize);
	}
	else {
		LOG_ERROR("save_sample_to_buffer: no valid sample data for sample %d", samplenum);
		out_buffer.clear();
		return false;
	}

	LOG_INFO("Sample '%s' exported to buffer (%zu bytes)", sample->name.c_str(), out_buffer.size());
	return true;
}

#ifdef USE_VUMETER
void mixer_get_vu(float* left, float* right)
{
	if (left)  *left = std::clamp(g_vuL.load(std::memory_order_relaxed), 0.0f, 1.0f);
	if (right) *right = std::clamp(g_vuR.load(std::memory_order_relaxed), 0.0f, 1.0f);
}

void mixer_reset_vu()
{
	g_vuL.store(0.0f, std::memory_order_relaxed);
	g_vuR.store(0.0f, std::memory_order_relaxed);
}
#endif

void mixer_set_master_volume(int volumePercent)
{
	volumePercent = std::clamp(volumePercent, 0, 100);
	g_master_pct = volumePercent;
	const float amplitude = VolumePercentToLinear(volumePercent);
	if (g_backend) g_backend->SetMasterVolume(amplitude);

	float dB = (amplitude > 0.0f) ? 20.0f * log10f(amplitude) : -1000.0f;
	LOG_INFO("Master volume: %d%% -> %.2f dB -> %.6f linear", volumePercent, dB, amplitude);
}

float mixer_get_master_volume()
{
	return g_backend ? g_backend->GetMasterVolume() : 1.0f;
}

int mixer_get_master_volume_percent()
{
	return g_master_pct;
}

int mixer_get_output_channels()
{
	return g_backend ? static_cast<int>(g_backend->OutputChannelCount()) : 0;
}

uint32_t mixer_get_output_channel_mask()
{
	return g_backend ? g_backend->OutputChannelMask() : 0u;
}

void samples_stop_all()
{
	std::scoped_lock lock(audioMutex);
	for (int i = 0; i < MAX_CHANNELS; ++i) {
		stop_channel_locked(i);
	}
	// stop_channel_locked removed each chanid from audio_list individually;
	// clear() is cheap insurance against any membership drift.
	audio_list.clear();
}

#pragma warning(disable : 4018)

// ------------------ small helpers for MP3 detection -------------------------------
// These are only used onthe off chance an MP3 doesn't strt with "MP3" in the ID tag.
// ----------------------------------------------------------------------------------
#ifdef MP3_DECODE
static inline bool has_id3v2(const unsigned char* p, size_t n) {
	return (n >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3');
}

static inline size_t skip_id3v2(const unsigned char* p, size_t n) {
	if (!has_id3v2(p, n)) return 0;
	// ID3v2 header is 10 bytes; size is syncsafe at bytes 6..9
	if (n < 10) return 0;
	size_t sz = ((p[6] & 0x7F) << 21) | ((p[7] & 0x7F) << 14) | ((p[8] & 0x7F) << 7) | (p[9] & 0x7F);
	return (sz > n - 10) ? 10 : (10 + sz); // guard overflow
}

static inline bool looks_like_mpeg_frame(const unsigned char* p, size_t n) {
	// Minimal sync: 0xFFF sync bits plus version/layer not all invalid
	if (n < 2) return false;
	return (p[0] == 0xFF) && ((p[1] & 0xE0) == 0xE0);
}
#endif

// =====================================================================
// WAV
// =====================================================================
int processWaveDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	if (bufferSize < 12) {
		LOG_ERROR("Invalid WAV buffer size.");
		return -1;
	}

	if (std::memcmp(buffer, "RIFF", 4) != 0 || std::memcmp(buffer + 8, "WAVE", 4) != 0) {
		LOG_ERROR("Invalid WAV header.");
		return -1;
	}

	size_t pos = 12;
	bool haveFmt = false;
	bool haveData = false;

	while (pos + 8 <= bufferSize) {
		char chunkID[5] = {};
		std::memcpy(chunkID, buffer + pos, 4);
		pos += 4;

		uint32_t chunkSize = 0;
		std::memcpy(&chunkSize, buffer + pos, sizeof(uint32_t));
		pos += 4;

		// Bounds check for the declared chunk size
		if (pos + chunkSize > bufferSize) {
			LOG_ERROR("WAV chunk overruns buffer (chunk '%c%c%c%c', size=%u)",
				chunkID[0], chunkID[1], chunkID[2], chunkID[3], chunkSize);
			return -1;
		}

		const size_t chunkStart = pos;

		if (std::strncmp(chunkID, "fmt ", 4) == 0) {
			if (chunkSize < 16) {
				LOG_ERROR("WAV fmt chunk too small.");
				return -1;
			}

			std::memcpy(&audioFile->fx.wFormatTag, buffer + pos, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.nChannels, buffer + pos + 2, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.nSamplesPerSec, buffer + pos + 4, sizeof(uint32_t));
			std::memcpy(&audioFile->fx.nAvgBytesPerSec, buffer + pos + 8, sizeof(uint32_t));
			std::memcpy(&audioFile->fx.nBlockAlign, buffer + pos + 12, sizeof(uint16_t));
			std::memcpy(&audioFile->fx.wBitsPerSample, buffer + pos + 14, sizeof(uint16_t));

			// Reject anything that isn't plain integer PCM. Other tags
			// (WAVE_FORMAT_IEEE_FLOAT=3, WAVE_FORMAT_EXTENSIBLE=0xFFFE,
			// WAVE_FORMAT_ALAW=6, WAVE_FORMAT_MULAW=7, ADPCM variants, etc.)
			// would silently feed non-PCM bytes through the 8/16-bit code paths
			// and produce noise. Callers should pre-convert these files.
			if (audioFile->fx.wFormatTag != WAVE_FORMAT_PCM) {
				LOG_ERROR("WAV: unsupported wFormatTag 0x%04X (only WAVE_FORMAT_PCM/1 is supported)",
					audioFile->fx.wFormatTag);
				return -1;
			}
			if (audioFile->fx.nChannels < 1 || audioFile->fx.nChannels > 2) {
				LOG_ERROR("WAV: unsupported channel count %u (only mono/stereo supported)",
					audioFile->fx.nChannels);
				return -1;
			}
			haveFmt = true;

			pos += chunkSize;
		}
		else if (std::strncmp(chunkID, "data", 4) == 0) {
			if (!haveFmt) {
				LOG_ERROR("WAV data before fmt.");
				return -1;
			}

			audioFile->dataSize = chunkSize;

			if (audioFile->fx.wBitsPerSample == 8) {
				audioFile->data8 = std::make_unique<uint8_t[]>(chunkSize);
				std::memcpy(audioFile->data8.get(), buffer + pos, chunkSize);
				audioFile->buffer = audioFile->data8.get();
				audioFile->sampleCount = static_cast<uint32_t>(chunkSize); // 8-bit = 1 byte/sample
				haveData = true;
			}
			else if (audioFile->fx.wBitsPerSample == 16) {
				size_t sampleCount = chunkSize / sizeof(int16_t);
				audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
				std::memcpy(audioFile->data16.get(), buffer + pos, chunkSize);
				audioFile->buffer = audioFile->data16.get();
				audioFile->sampleCount = static_cast<uint32_t>(sampleCount);
				haveData = true;
			}
			else {
				LOG_INFO("Unsupported bit depth: %d", audioFile->fx.wBitsPerSample);
				return -1;
			}

			pos += chunkSize;
		}
		else {
			// Skip unknown chunk payload
			pos += chunkSize;
		}

		// Pad to even boundary per RIFF spec
		if ((pos & 1u) != 0) {
			++pos;
		}

		// If we've read both fmt and data, we can stop
		if (haveFmt && haveData) break;

		// Guard against stalled loop
		if (pos <= chunkStart) {
			LOG_ERROR("WAV parser stalled.");
			return -1;
		}
	}

	audioFile->fx.cbSize = 0; // PCM baseline
	return haveData ? 0 : -1;
}

// =====================================================================
// MP3
// =====================================================================
#ifdef MP3_DECODE
int processMp3DataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	mp3dec_t mp3d;
	mp3dec_file_info_t info{};
	mp3dec_init(&mp3d);

	if (mp3dec_load_buf(&mp3d, buffer, bufferSize, &info, nullptr, nullptr) != 0) {
		LOG_ERROR("MP3 decode failed.");
		return -1;
	}

	audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;
	audioFile->fx.nChannels = info.channels;
	audioFile->fx.nSamplesPerSec = info.hz;
	audioFile->fx.wBitsPerSample = 16;
	audioFile->fx.nBlockAlign = info.channels * 2;
	audioFile->fx.nAvgBytesPerSec = info.hz * audioFile->fx.nBlockAlign;
	audioFile->fx.cbSize = 0; // important for PCM

	size_t sampleCount = info.samples; // total interleaved samples
	audioFile->dataSize = sampleCount * sizeof(int16_t);
	audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
	std::memcpy(audioFile->data16.get(), info.buffer, audioFile->dataSize);
	audioFile->buffer = audioFile->data16.get();
	audioFile->sampleCount = static_cast<uint32_t>(sampleCount);

	free(info.buffer);
	return 0;
}
#endif

// =====================================================================
// OGG
// =====================================================================
#ifdef OGG_DECODE
int processOggDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	int error = 0;
	stb_vorbis* vorbis = stb_vorbis_open_memory(buffer, static_cast<int>(bufferSize), &error, nullptr);
	if (!vorbis || error) {
		LOG_ERROR("OGG decode failed.");
		return -1;
	}

	stb_vorbis_info info = stb_vorbis_get_info(vorbis);

	audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;
	audioFile->fx.nChannels = info.channels;
	audioFile->fx.nSamplesPerSec = info.sample_rate;
	audioFile->fx.wBitsPerSample = 16;
	audioFile->fx.nBlockAlign = info.channels * 2;
	audioFile->fx.nAvgBytesPerSec = info.sample_rate * audioFile->fx.nBlockAlign;
	audioFile->fx.cbSize = 0;

	// Total frames (per-channel samples), then convert to interleaved sample count
	const int totalFrames = stb_vorbis_stream_length_in_samples(vorbis);
	const size_t totalSamples = static_cast<size_t>(totalFrames) * info.channels;

	audioFile->dataSize = totalSamples * sizeof(int16_t);
	audioFile->data16 = std::make_unique<int16_t[]>(totalSamples);
	audioFile->buffer = audioFile->data16.get();
	audioFile->sampleCount = static_cast<uint32_t>(totalSamples);

	// IMPORTANT: pass "frames" (per-channel count), not interleaved total samples
	stb_vorbis_get_samples_short_interleaved(vorbis, info.channels,
		audioFile->data16.get(), totalFrames);

	stb_vorbis_close(vorbis);
	return 0;
}
#endif

// =====================================================================
// Top-level loader: returns 1 on success, 0 on failure
// =====================================================================
int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile)
{
	const unsigned char* p = buffer;
	const size_t n = static_cast<size_t>(fileSize);

	// 1) WAV?
	if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WAVE", 4) == 0) {
		LOG_INFO("Processing as WAV.");
		return (processWaveDataBuffer(p, n, audioFile) == 0) ? 1 : 0;
	}

	// 2) OGG?
#ifdef OGG_DECODE
	if (n >= 4 && std::memcmp(p, "OggS", 4) == 0) {
		LOG_INFO("Processing as OGG...");
		return (processOggDataBuffer(p, n, audioFile) == 0) ? 1 : 0;
	}
#endif

	// 3) MP3? (ID3 or raw MPEG frames)
#ifdef MP3_DECODE
	{
		size_t off = skip_id3v2(p, n);
		const unsigned char* q = (off < n) ? (p + off) : p;
		const size_t m = (off < n) ? (n - off) : n;

		if ((n >= 3 && std::memcmp(p, "ID3", 3) == 0) || looks_like_mpeg_frame(q, m)) {
			LOG_INFO("Processing as MP3...");
			return (processMp3DataBuffer(p, n, audioFile) == 0) ? 1 : 0;
		}
	}
#endif

	LOG_ERROR("Unknown audio format (no RIFF/WAVE, no OggS, no ID3/MPEG sync)");
	return 0;
}

// RESAMPLE

// ============================== Utilities ====================================

double dBToAmplitude(double db)
{
	return std::pow(10.0, db / 20.0);
}

void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB)
{
	if (!samples || num_samples == 0) return;
	const double factor = std::pow(10.0, static_cast<double>(dB) / 20.0);
	for (size_t i = 0; i < num_samples; ++i) {
		const double s = static_cast<double>(samples[i]) * factor;
		const int32_t y = static_cast<int32_t>(std::llround(s));
		samples[i] = static_cast<int16_t>(std::clamp(
			y, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX)));
	}
}

static inline int16_t saturate_i16(int32_t v)
{
	if (v > INT16_MAX) return INT16_MAX;
	if (v < INT16_MIN) return INT16_MIN;
	return static_cast<int16_t>(v);
}

static inline int16_t sample_at_safe_i16(const int16_t* s, int32_t n, int32_t idx)
{
	if (idx < 0) idx = 0;
	if (idx >= n) idx = n - 1;
	return s[idx];
}

static inline double catmull_rom_scalar(double p0, double p1, double p2, double p3, double x)
{
	const double a0 = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
	const double a1 = 1.0 * p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
	const double a2 = -0.5 * p0 + 0.5 * p2;
	const double a3 = p1;
	return ((a0 * x + a1) * x + a2) * x + a3;
}

// ============================= Core resamplers ================================
// Endpoint-aligned mapping (i=0-src0, i=outN-1->srcN-1). OOB-safe.

static inline void linear_into_core(const int16_t* in, int32_t inN,
	int16_t* out, int32_t outN)
{
	if (inN <= 0 || outN <= 0) return;

	if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
	if (outN == 1) { out[0] = in[0]; return; }

	// 32.32 fixed-point position. step is chosen so that pos = i * step
	// reaches (inN-1) << 32 at i = outN-1 (modulo integer-division rounding),
	// giving the same endpoint-aligned mapping as the float reference.
	// Linear interpolation between two int16 values is provably in int16
	// range, so no saturation is needed; rounding is half-LSB-up.
	const uint64_t step = (static_cast<uint64_t>(inN - 1) << 32)
	                      / static_cast<uint64_t>(outN - 1);
	uint64_t pos = 0;

	const int32_t last_idx = inN - 1;
	for (int32_t i = 0; i < outN; ++i) {
		const int32_t idx = static_cast<int32_t>(pos >> 32);
		if (idx >= last_idx) {
			out[i] = in[last_idx];
		} else {
			// Q15 fraction = top 15 bits of the 32 fractional bits of pos.
			const int32_t frac_q15 = static_cast<int32_t>((pos >> 17) & 0x7FFF);
			const int32_t a = in[idx];
			const int32_t b = in[idx + 1];
			const int32_t scaled = (b - a) * frac_q15 + (1 << 14);
			out[i] = static_cast<int16_t>(a + (scaled >> 15));
		}
		pos += step;
	}
}

static inline void cubic_into_core(const int16_t* in, int32_t inN,
	int16_t* out, int32_t outN)
{
	if (inN <= 0 || outN <= 0) return;

	if (inN == 1) { std::fill(out, out + outN, in[0]); return; }
	if (outN == 1) { out[0] = in[0]; return; }

	const double scale = static_cast<double>(inN - 1) / static_cast<double>(outN - 1);

	for (int32_t i = 0; i < outN; ++i) {
		const double src_pos = static_cast<double>(i) * scale;
		int32_t idx = static_cast<int32_t>(std::floor(src_pos));
		double  frac = src_pos - static_cast<double>(idx);

		if (idx >= inN - 1) { idx = inN - 2; frac = 1.0; }

		const double p0 = static_cast<double>(sample_at_safe_i16(in, inN, idx - 1));
		const double p1 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 0));
		const double p2 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 1));
		const double p3 = static_cast<double>(sample_at_safe_i16(in, inN, idx + 2));

		const double y = catmull_rom_scalar(p0, p1, p2, p3, frac);
		out[i] = saturate_i16(static_cast<int32_t>(std::llround(y)));
	}
}

// =========================== Non-allocating wrappers ==========================

void linear_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples)
{
	if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
	linear_into_core(input_data, input_samples, output_data, output_samples);
}

void cubic_interpolation_16_into(const int16_t* input_data,
	int32_t input_samples,
	int16_t* output_data,
	int32_t output_samples)
{
	if (!input_data || !output_data || input_samples <= 0 || output_samples <= 0) return;
	cubic_into_core(input_data, input_samples, output_data, output_samples);
}

// =================== Allocating resamplers ====================

void linear_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio)
{
	if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
		if (output_data)    *output_data = nullptr;
		if (output_samples) *output_samples = 0;
		return;
	}

	int32_t outN = static_cast<int32_t>(
		std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
	if (outN < 1) outN = 1;

	if (*output_data && *output_samples != outN) {
		delete[] * output_data;
		*output_data = nullptr;
	}
	if (!*output_data) {
		*output_data = new int16_t[outN];
	}
	*output_samples = outN;

	linear_into_core(input_data, input_samples, *output_data, outN);
}

void cubic_interpolation_16(const int16_t* input_data,
	int32_t input_samples,
	int16_t** output_data,
	int32_t* output_samples,
	float ratio)
{
	if (!output_data || !output_samples || !input_data || input_samples <= 0 || ratio <= 0.0f) {
		if (output_data)    *output_data = nullptr;
		if (output_samples) *output_samples = 0;
		return;
	}

	int32_t outN = static_cast<int32_t>(
		std::floor(static_cast<double>(input_samples) * static_cast<double>(ratio)));
	if (outN < 1) outN = 1;

	if (*output_data && *output_samples != outN) {
		delete[] * output_data;
		*output_data = nullptr;
	}
	if (!*output_data) {
		*output_data = new int16_t[outN];
	}
	*output_samples = outN;

	cubic_into_core(input_data, input_samples, *output_data, outN);
}

// ============================ 8-bit  ==============================

void linear_interpolation_8(const uint8_t* input,
	uint8_t* output,
	int input_size,
	int output_size)
{
	if (!input || !output || input_size <= 0 || output_size <= 0) return;

	if (input_size == 1) { std::fill(output, output + output_size, input[0]); return; }
	if (output_size == 1) { output[0] = input[0]; return; }

	// 32.32 fixed-point position; see linear_into_core for the rationale.
	// Interpolating between two uint8 values produces a value in [0, 255],
	// so no clamping is needed; rounding is half-LSB-up via the +(1<<14)
	// offset before the Q15 shift.
	const uint64_t step = (static_cast<uint64_t>(input_size - 1) << 32)
	                      / static_cast<uint64_t>(output_size - 1);
	uint64_t pos = 0;

	const int last_idx = input_size - 1;
	for (int i = 0; i < output_size; ++i) {
		const int idx = static_cast<int>(pos >> 32);
		if (idx >= last_idx) {
			output[i] = input[last_idx];
		} else {
			const int32_t frac_q15 = static_cast<int32_t>((pos >> 17) & 0x7FFF);
			const int32_t a = input[idx];
			const int32_t b = input[idx + 1];
			const int32_t scaled = (b - a) * frac_q15 + (1 << 14);
			output[i] = static_cast<uint8_t>(a + (scaled >> 15));
		}
		pos += step;
	}
}

// ============================ Optional postfilter =============================

void lowpass_postfilter_16(int16_t* data, int32_t samples)
{
	if (!data || samples <= 4) return;

	int16_t* temp = new int16_t[samples];

	for (int32_t i = 0; i < samples; ++i) {
		const int32_t s0 = data[(i - 2 < 0) ? 0 : i - 2];
		const int32_t s1 = data[(i - 1 < 0) ? 0 : i - 1];
		const int32_t s2 = data[i];
		const int32_t s3 = data[(i + 1 >= samples) ? samples - 1 : i + 1];
		const int32_t s4 = data[(i + 2 >= samples) ? samples - 1 : i + 2];

		const int32_t num = (-s0) + (4 * s1) + (6 * s2) + (4 * s3) - s4;
		const int32_t y = static_cast<int32_t>(
			std::lrint(static_cast<double>(num) / 12.0));
		temp[i] = saturate_i16(y);
	}

	std::copy(temp, temp + samples, data);
	delete[] temp;
}

//
// WAV Filters
//

void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate) {
	if (audioSample.empty()) return;

	float RC = 1.0f / (cutoffFreq * 2.0f * static_cast<float>(M_PI));
	float dt = 1.0f / sampleRate;
	float alpha = RC / (RC + dt);

	int16_t previousSample = audioSample[0];
	int16_t previousFilteredSample = audioSample[0];

	for (size_t i = 1; i < audioSample.size(); ++i) {
		int16_t currentSample = audioSample[i];
		audioSample[i] = static_cast<int16_t>(alpha * (previousFilteredSample + currentSample - previousSample));
		previousFilteredSample = audioSample[i];
		previousSample = currentSample;
	}
}

void lowPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate) {
	if (audioSample.empty()) return;

	float RC = 1.0f / (cutoffFreq * 2.0f * static_cast<float>(M_PI));
	float dt = 1.0f / sampleRate;
	float alpha = dt / (RC + dt);

	int16_t previousSample = audioSample[0];

	for (size_t i = 1; i < audioSample.size(); ++i) {
		audioSample[i] = static_cast<int16_t>(previousSample + alpha * (audioSample[i] - previousSample));
		previousSample = audioSample[i];
	}
}

void design_biquad_lowpass(float fs, float fc, float Q, float& b0, float& b1, float& b2, float& a1, float& a2)
{
	const float w0 = 2.0f * float(M_PI) * fc / fs;
	const float cosw = std::cos(w0);
	const float sinw = std::sin(w0);
	const float alpha = sinw / (2.0f * Q);

	float b0u = (1.0f - cosw) * 0.5f;
	float b1u = 1.0f - cosw;
	float b2u = (1.0f - cosw) * 0.5f;
	float a0u = 1.0f + alpha;
	float a1u = -2.0f * cosw;
	float a2u = 1.0f - alpha;

	b0 = b0u / a0u;
	b1 = b1u / a0u;
	b2 = b2u / a0u;
	a1 = a1u / a0u;
	a2 = a2u / a0u;
}

void biquad_lowpass_inplace_i16(int16_t* x, int n, float fs, float fc, float Q, int passes)
{
	if (!x || n <= 2 || fc <= 0.0f || fs <= 0.0f) return;
	float b0, b1, b2, a1, a2;
	design_biquad_lowpass(fs, fc, Q, b0, b1, b2, a1, a2);

	for (int p = 0; p < passes; ++p)
	{
		float x1 = 0.0f, x2 = 0.0f;
		float y1 = 0.0f, y2 = 0.0f;

		for (int i = 0; i < n; ++i)
		{
			const float xf = (float)x[i];
			const float y = b0 * xf + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

			x2 = x1; x1 = xf;
			y2 = y1; y1 = y;

			int v = (int)std::lrintf(y);
			x[i] = (int16_t)std::clamp(v, -32768, 32767);
		}
	}
}

// WAV SWEEP FUNCTIONS

// --------------------------- Internal Types ----------------------------------

namespace {
	enum class Param : uint8_t { Volume = 0, Pan = 1, Freq = 2 };

	struct Key {
		int voice = -1;
		Param param = Param::Volume;

		bool operator==(const Key& o) const noexcept {
			return voice == o.voice && param == o.param;
		}
	};

	struct KeyHash {
		std::size_t operator()(const Key& k) const noexcept {
			return (static_cast<std::size_t>(k.voice) << 2)
				^ static_cast<std::size_t>(static_cast<uint8_t>(k.param));
		}
	};

	struct Sweep {
		int   voice = -1;
		Param param = Param::Volume;
		int   start = 0;
		int   end = 0;
		int   duration_ms = 0;
		std::chrono::steady_clock::time_point t0;
	};

	constexpr int kTickMs = 1;
	std::atomic<bool> g_started{ false };
	std::atomic<bool> g_stop{ false };
	std::thread g_worker;

	std::mutex g_mtx;
	std::condition_variable g_cv;

	std::unordered_map<Key, Sweep, KeyHash> g_sweeps;
	std::unordered_map<int, int> g_lastFreqHz;

	void worker_loop();
	void ensure_started();
	int  clamp_int(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

	int get_current_value(int voice, Param p)
	{
		switch (p) {
		case Param::Volume: return clamp_int(sample_get_volume(voice), 0, 255);
		case Param::Pan:    return clamp_int(sample_get_pan(voice), 0, 255);
		case Param::Freq: {
			auto it = g_lastFreqHz.find(voice);
			if (it != g_lastFreqHz.end()) return it->second;
			int base = sample_get_freq(voice);
			return (base > 0) ? base : 0;
		}
		}
		return 0;
	}

	void apply_value(int voice, Param p, int v)
	{
		switch (p) {
		case Param::Volume:
			sample_set_volume(voice, clamp_int(v, 0, 255));
			break;
		case Param::Pan:
			sample_set_pan(voice, clamp_int(v, 0, 255));
			break;
		case Param::Freq:
			if (v < 1) v = 1;
			sample_set_freq(voice, v);
			{
				std::lock_guard<std::mutex> lk(g_mtx);
				g_lastFreqHz[voice] = v;
			}
			break;
		}
	}

	void ensure_started()
	{
		bool expected = false;
		if (g_started.compare_exchange_strong(expected, true)) {
			g_stop = false;
			g_worker = std::thread(worker_loop);

			std::atexit([] {
				try { wavsweep_shutdown(); }
				catch (...) {}
				});
		}
	}

	void worker_loop()
	{
		LOG_INFO("wav_sweep: worker thread started");

		std::unique_lock<std::mutex> lk(g_mtx);

		while (!g_stop.load(std::memory_order_relaxed)) {
			g_cv.wait_for(lk, std::chrono::milliseconds(kTickMs),
				[] { return g_stop.load(std::memory_order_relaxed); });
			if (g_stop.load(std::memory_order_relaxed))
				break;

			const auto now = std::chrono::steady_clock::now();
			std::vector<Sweep> ops;
			ops.reserve(g_sweeps.size());

			for (auto it = g_sweeps.begin(); it != g_sweeps.end(); ) {
				Sweep& s = it->second;

				const int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.t0).count();
				const double  T = (s.duration_ms <= 0) ? 1.0 : std::clamp(elapsed / static_cast<double>(s.duration_ms), 0.0, 1.0);

				const double cur = s.start + (s.end - s.start) * T;
				const int    ival = static_cast<int>(std::lround(cur));

				ops.push_back({ s.voice, s.param, ival, ival, 0, s.t0 });

				if (T >= 1.0) {
					it = g_sweeps.erase(it);
				}
				else {
					++it;
				}
			}

			lk.unlock();
			for (const auto& op : ops) {
				apply_value(op.voice, op.param, op.start);
			}
			lk.lock();
		}

		LOG_INFO("wav_sweep: worker thread exiting");
	}
} // anonymous namespace

// --------------------------- Public API --------------------------------------

void wavsweep_init()
{
	ensure_started();
}

void wavsweep_shutdown()
{
	if (!g_started.load()) return;

	g_stop = true;
	g_cv.notify_all();
	if (g_worker.joinable())
		g_worker.join();

	std::lock_guard<std::mutex> lk(g_mtx);
	g_sweeps.clear();
	g_lastFreqHz.clear();
	g_started = false;
}

static void start_sweep(int voice, Param param, int time_ms, int end_value)
{
	ensure_started();

	if (time_ms <= 0) {
		apply_value(voice, param, end_value);
		return;
	}

	Sweep s;
	s.voice = voice;
	s.param = param;
	s.duration_ms = time_ms;
	s.end = end_value;
	s.t0 = std::chrono::steady_clock::now();
	s.start = get_current_value(voice, param);

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_sweeps[{voice, param}] = s;
	}

	g_cv.notify_one();
}

void mixer_ramp_volume(int voice, int time_ms, int endvol)
{
	endvol = clamp_int(endvol, 0, 255);
	start_sweep(voice, Param::Volume, time_ms, endvol);
}

void mixer_sweep_frequency(int voice, int time_ms, int endfreq)
{
	if (endfreq < 1) endfreq = 1;
	start_sweep(voice, Param::Freq, time_ms, endfreq);
}

void mixer_sweep_pan(int voice, int time_ms, int endpan)
{
	endpan = clamp_int(endpan, 0, 255);
	start_sweep(voice, Param::Pan, time_ms, endpan);
}
