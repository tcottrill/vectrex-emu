// =============================================================================
// xaudio2_backend.h
// Audio streaming backend abstraction.
//
// IAudioBackend is the interface mixer.cpp uses to push interleaved S16 stereo
// audio to the OS. It hides the engine (XAudio2 today, WASAPI / something else
// tomorrow) from the software mixer.
//
// XAudio2Backend is the concrete implementation. It also exposes GetEngine()
// for the voice-path code in mixer.cpp, which talks to XAudio2 directly
// (per-channel IXAudio2SourceVoice with hardware pitch/loop/output matrix).
// That accessor is XAudio2-specific by design; a future WASAPI backend would
// not provide it, and the voice-path APIs would gate themselves off.
// =============================================================================
#pragma once

#include <cstdint>

#ifndef WIN7BUILD
#include <xaudio2.h>
#else
#include <xaudio2redist.h>
#endif

class IAudioBackend {
public:
	virtual ~IAudioBackend() = default;

	// Build engine + mastering voice + output streaming voice + ring buffers.
	// Returns S_OK on success. On failure, internal state is fully torn down.
	virtual HRESULT Init(int rateHz, int fps) = 0;

	// Idempotent. Safe to call from destructors.
	virtual void Shutdown() = 0;

	// Returns the next ring-buffer slot for the mixer to fill.
	virtual BYTE* GetNextBuffer() = 0;

	// Submit a filled buffer to the output device. If the device is backed up
	// (ring buffer full), the backend may drop the frame and return S_OK.
	virtual HRESULT Submit(BYTE* buffer, DWORD bytes) = 0;

	// Master output gain, linear 0..1. SetMasterVolume(curve_applied_value).
	virtual void  SetMasterVolume(float linear) = 0;
	virtual float GetMasterVolume() const = 0;

	// Output channel count and SPEAKER_xxx mask of the mastering voice.
	// Captured after Init from XAudio2's GetVoiceDetails / GetChannelMask.
	// Used by X3DAudio init and by per-voice SetOutputMatrix sizing.
	virtual uint32_t OutputChannelCount() const = 0;
	virtual uint32_t OutputChannelMask() const = 0;

	int OutputRate() const { return m_rate; }
	int FramesPerUpdate() const { return m_frames_per_update; }

protected:
	int m_rate = 0;
	int m_fps = 0;
	int m_frames_per_update = 0;
};

class XAudio2Backend : public IAudioBackend {
public:
	XAudio2Backend() = default;
	~XAudio2Backend() override { Shutdown(); }

	HRESULT Init(int rateHz, int fps) override;
	void    Shutdown() override;

	BYTE*   GetNextBuffer() override;
	HRESULT Submit(BYTE* buffer, DWORD bytes) override;

	void    SetMasterVolume(float linear) override;
	float   GetMasterVolume() const override;

	uint32_t OutputChannelCount() const override { return m_output_channels; }
	uint32_t OutputChannelMask() const override { return m_output_channel_mask; }

	// XAudio2-specific accessor for the voice path in mixer.cpp. Returns the
	// engine handle so mixer.cpp can CreateSourceVoice for per-channel direct
	// playback. Not part of IAudioBackend - swapping backends means voice
	// path must be adapted.
	IXAudio2* GetEngine() const { return m_xaudio2; }

private:
	static constexpr int kNumBuffers = 5;

	IXAudio2*               m_xaudio2 = nullptr;
	IXAudio2MasteringVoice* m_master = nullptr;
	IXAudio2SourceVoice*    m_source = nullptr;
	BYTE*                   m_buffers[kNumBuffers]{};
	DWORD                   m_buffer_size = 0;
	int                     m_current = 0;
	bool                    m_com_init_local = false;
	uint32_t                m_output_channels = 0;
	uint32_t                m_output_channel_mask = 0;
};
