// =============================================================================
// audio_3d.cpp
// X3DAudio glue for the voice path. Holds a single global listener (camera-
// locked) and applies per-voice matrices on demand. State is module-local and
// guarded by the mixer's audioMutex from the call sites.
// =============================================================================
#include "audio_3d.h"
#include "sys_log.h"
#include <cstdio>

// Force-link X3DAudio. XAudio2's symbols are pulled in by mixer.cpp /
// xaudio2_backend.cpp (or by xaudio2redist.h's own pragmas), but X3DAudio
// lives in a different lib depending on which header you used.
#ifdef WIN7BUILD
// xaudio2redist NuGet package bundles X3DAudio in the same lib.
#pragma comment(lib, "xaudio2_9redist.lib")
#else
// Modern Windows 10/11 SDK: X3DAudio is part of xaudio2.lib.
#pragma comment(lib, "xaudio2.lib")
#endif

namespace {
	X3DAUDIO_HANDLE   g_x3d;
	uint32_t          g_dst_channels = 0;
	uint32_t          g_channel_mask = 0;
	bool              g_inited = false;
	X3DAUDIO_LISTENER g_listener{};

	// Scratch matrix storage sized for the worst case we support:
	// 2 source channels (stereo) * 8 destination channels (7.1) = 16 floats.
	constexpr uint32_t kMaxSrcCh = 2;
	constexpr uint32_t kMaxDstCh = 8;
	float g_matrix[kMaxSrcCh * kMaxDstCh];

	// Stereo emitters need a per-channel azimuth array. Both zero -> both
	// channels positioned at the emitter point (treated as a mono point source
	// for spatialization purposes). Good enough for SFX in a 2D game.
	float g_stereo_azimuths[2] = { 0.0f, 0.0f };

	bool g_debug_print_next = false;

	// Returns a short label for the Nth output channel given the current mask,
	// e.g. for mask 0x60F and idx 4 -> "SL".
	const char* channel_label(uint32_t idx)
	{
		struct Bit { uint32_t bit; const char* name; };
		static const Bit bits[] = {
			{ SPEAKER_FRONT_LEFT,            "FL"  },
			{ SPEAKER_FRONT_RIGHT,           "FR"  },
			{ SPEAKER_FRONT_CENTER,          "FC"  },
			{ SPEAKER_LOW_FREQUENCY,         "LFE" },
			{ SPEAKER_BACK_LEFT,             "BL"  },
			{ SPEAKER_BACK_RIGHT,            "BR"  },
			{ SPEAKER_FRONT_LEFT_OF_CENTER,  "FLC" },
			{ SPEAKER_FRONT_RIGHT_OF_CENTER, "FRC" },
			{ SPEAKER_BACK_CENTER,           "BC"  },
			{ SPEAKER_SIDE_LEFT,             "SL"  },
			{ SPEAKER_SIDE_RIGHT,            "SR"  },
		};
		uint32_t seen = 0;
		for (const auto& b : bits) {
			if (g_channel_mask & b.bit) {
				if (seen == idx) return b.name;
				++seen;
			}
		}
		return "?";
	}
}

void audio_3d_debug_print_next_matrix() { g_debug_print_next = true; }

uint32_t audio_3d_get_channel_mask() { return g_channel_mask; }

bool audio_3d_init(uint32_t channel_mask, uint32_t dst_channels)
{
	if (g_inited) audio_3d_shutdown();

	if (dst_channels == 0 || dst_channels > kMaxDstCh) {
		LOG_ERROR("audio_3d_init: dst_channels=%u out of supported range (1..%u)",
			dst_channels, kMaxDstCh);
		return false;
	}
	if (channel_mask == 0) {
		// Some endpoints report mask=0 (e.g., bare stereo with no metadata).
		// X3DAudioInitialize rejects mask=0. Synthesize a sensible default.
		// Constants are KSAUDIO_SPEAKER_xxx equivalents, defined locally to
		// avoid pulling in <ksmedia.h>.
		constexpr uint32_t MASK_5POINT1 =
			SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
			SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; // 0x3F
		constexpr uint32_t MASK_7POINT1_SURROUND =
			SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
			SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
			SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;                          // 0x63F
		LOG_INFO("audio_3d_init: channel_mask=0, defaulting based on channel count");
		switch (dst_channels) {
		case 1:  channel_mask = SPEAKER_FRONT_CENTER; break;
		case 2:  channel_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; break;
		case 6:  channel_mask = MASK_5POINT1; break;
		case 8:  channel_mask = MASK_7POINT1_SURROUND; break;
		default: channel_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; break;
		}
	}

	// Windows reports the endpoint as 0x60F (5.1 SURROUND, SL/SR) for 5.1
	// configurations even when the user's physical speakers are back-rear
	// (BL/BR), which is the common case for real 5.1 setups. That mismatch
	// also drives an X3DAudio matrix bug: with 0x60F, an emitter at exactly
	// the SIDE speaker azimuth (+/-pi/2) sits on a boundary in X3DAudio's
	// internal speaker table where the left-side pair search leaks energy
	// to FL instead of SL (same algorithm visible in FAudio's F3DAudio.c
	// k5Point1SurroundConfigSpeakers). Coercing to 0x3F (legacy 5.1 with
	// BL/BR at +/-110 deg) gives X3DAudio a speaker layout that (a) matches
	// the user's actual hardware and (b) avoids the boundary singularity.
	// The channel order is identical between 0x60F and 0x3F (FL,FR,FC,LFE,
	// rear-L,rear-R), so matrix coefficients still land on the correct
	// physical speakers when XAudio2 forwards them to the mastering voice.
	constexpr uint32_t MASK_5POINT1          = 0x3Fu;  // FL|FR|FC|LFE|BL|BR
	constexpr uint32_t MASK_5POINT1_SURROUND = 0x60Fu; // FL|FR|FC|LFE|SL|SR
	if (channel_mask == MASK_5POINT1_SURROUND) {
		LOG_INFO("audio_3d_init: OS reported mask 0x60F (SL/SR); using 0x3F (BL/BR) which matches typical 5.1 hardware and avoids X3DAudio's side-speaker boundary bug");
		channel_mask = MASK_5POINT1;
	}

	HRESULT hr = X3DAudioInitialize(channel_mask, X3DAUDIO_SPEED_OF_SOUND, g_x3d);
	if (FAILED(hr)) {
		LOG_ERROR("X3DAudioInitialize failed: 0x%08X", (unsigned)hr);
		return false;
	}

	g_dst_channels = dst_channels;
	g_channel_mask = channel_mask;

	// Listener defaults: at origin, facing +Z (which we map from 2D +Y, i.e.
	// "up on screen"), top is +Y (in 3D-space, which is unrelated to 2D Y).
	g_listener.Position    = { 0.0f, 0.0f, 0.0f };
	g_listener.OrientFront = { 0.0f, 0.0f, 1.0f };
	g_listener.OrientTop   = { 0.0f, 1.0f, 0.0f };
	g_listener.Velocity    = { 0.0f, 0.0f, 0.0f };

	g_inited = true;
	LOG_INFO("audio_3d_init: %u output channels, mask=0x%08X", dst_channels, channel_mask);
	return true;
}

void audio_3d_shutdown()
{
	g_inited = false;
	g_dst_channels = 0;
}

bool audio_3d_ready()
{
	return g_inited;
}

void audio_3d_set_listener_2d(float x, float y)
{
	if (!g_inited) return;
	g_listener.Position = { x, 0.0f, y };
}

bool audio_3d_apply_2d(IXAudio2SourceVoice* voice,
	float src_x, float src_y,
	uint32_t src_channels)
{
	if (!g_inited || !voice) return false;
	if (src_channels < 1 || src_channels > kMaxSrcCh) {
		LOG_ERROR("audio_3d_apply_2d: src_channels=%u not supported (mono/stereo only)", src_channels);
		return false;
	}

	X3DAUDIO_EMITTER emitter{};
	emitter.Position            = { src_x, 0.0f, src_y };
	emitter.OrientFront         = { 0.0f, 0.0f, 1.0f };
	emitter.OrientTop           = { 0.0f, 1.0f, 0.0f };
	emitter.Velocity            = { 0.0f, 0.0f, 0.0f };
	emitter.ChannelCount        = src_channels;
	emitter.ChannelRadius       = 1.0f;
	emitter.CurveDistanceScaler = 1.0f;
	emitter.DopplerScaler       = 0.0f;     // no doppler for now
	emitter.InnerRadius         = 0.0f;
	emitter.InnerRadiusAngle    = 0.0f;
	if (src_channels == 2) {
		emitter.pChannelAzimuths = g_stereo_azimuths;
	}

	X3DAUDIO_DSP_SETTINGS settings{};
	settings.SrcChannelCount     = src_channels;
	settings.DstChannelCount     = g_dst_channels;
	settings.pMatrixCoefficients = g_matrix;

	X3DAudioCalculate(g_x3d, &g_listener, &emitter,
		X3DAUDIO_CALCULATE_MATRIX, &settings);

	if (g_debug_print_next) {
		g_debug_print_next = false;
		std::printf("[X3D] src=(%.2f,0,%.2f)  listener=(%.2f,0,%.2f)  src_ch=%u dst_ch=%u\n",
			emitter.Position.x, emitter.Position.z,
			g_listener.Position.x, g_listener.Position.z,
			src_channels, g_dst_channels);
		// Matrix layout per XAudio2 / X3DAudio docs is m[SourceChannels*D + S];
		// destination-major (one row per destination channel). Print transposed
		// (src as outer loop, dst as inner) for human readability.
		for (uint32_t s = 0; s < src_channels; ++s) {
			std::printf("       src%u ->", s);
			for (uint32_t d = 0; d < g_dst_channels; ++d) {
				std::printf(" %s=%.3f", channel_label(d), g_matrix[src_channels * d + s]);
			}
			std::printf("\n");
		}
		std::fflush(stdout);
	}

	HRESULT hr = voice->SetOutputMatrix(nullptr, src_channels, g_dst_channels, g_matrix);
	if (FAILED(hr)) {
		LOG_ERROR("audio_3d_apply_2d: SetOutputMatrix failed: 0x%08X", (unsigned)hr);
		return false;
	}
	return true;
}
