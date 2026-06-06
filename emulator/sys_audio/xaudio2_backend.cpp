// =============================================================================
// xaudio2_backend.cpp
// Moved verbatim from mixer.cpp's former xaudio2_init / xaudio2_update /
// xaudio2_stop / GetNextBuffer block. Behavior is unchanged; the file-scope
// globals (pXAudio2, pMasterVoice, pSourceVoice, audioBuffers, bufferSize,
// currentBufferIndex, g_comInitLocal) are now private members.
//
// Master volume default is NOT set here - mixer.cpp owns the volume curve and
// applies the 80% default through its own mixer_set_master_volume after Init
// returns. Init only stands up the streaming infrastructure.
// =============================================================================
#include "xaudio2_backend.h"
#include "sys_log.h"
#include <cstring>

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

HRESULT XAudio2Backend::Init(int rateHz, int fps)
{
	HRESULT hr;

	m_rate = rateHz;
	m_fps = fps;
	m_frames_per_update = rateHz / fps;             // fixed integer frames per update

	const int remainder = rateHz % fps;
	if (remainder != 0) {
		LOG_INFO("XAudio2Backend::Init: %d Hz / %d FPS leaves remainder %d (using %d frames per update)",
			rateHz, fps, remainder, m_frames_per_update);
	}

	HRESULT hrCI = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (hrCI == S_OK || hrCI == S_FALSE) m_com_init_local = true;

	HR(XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR));
	HR(m_xaudio2->CreateMasteringVoice(&m_master, XAUDIO2_DEFAULT_CHANNELS, rateHz, 0, 0));

	// Capture the actual output layout. With XAUDIO2_DEFAULT_CHANNELS the
	// channel count comes from the OS-configured endpoint (a stereo endpoint
	// reports 2; a 5.1 endpoint reports 6; a stereo endpoint with Windows
	// Sonic for Headphones enabled reports 8 because Sonic exposes itself
	// as 7.1 to applications and renders to stereo downstream).
	{
		XAUDIO2_VOICE_DETAILS details{};
		m_master->GetVoiceDetails(&details);
		m_output_channels = details.InputChannels;
		DWORD mask = 0;
		if (SUCCEEDED(m_master->GetChannelMask(&mask))) {
			m_output_channel_mask = mask;
		}
		LOG_INFO("XAudio2Backend::Init: master %u channels, mask=0x%08X",
			m_output_channels, m_output_channel_mask);
	}

	// Stereo 16-bit PCM source voice
	WAVEFORMATEX wf = {};
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 2;
	wf.nSamplesPerSec = rateHz;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8; // 4 bytes per stereo frame
	wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
	wf.cbSize = 0;

	hr = m_xaudio2->CreateSourceVoice(&m_source, &wf, XAUDIO2_VOICE_NOPITCH,
		XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
	if (FAILED(hr)) {
		LOG_INFO("Failed to create source voice: %#X", hr);
		Shutdown();
		return hr;
	}

	const int buffer_duration_ms = 1000 / fps;
	LOG_INFO("XAudio2Backend::Init: FramesPerUpdate=%d (~%d ms per update)",
		m_frames_per_update, buffer_duration_ms);

	// Allocate ring buffers: frames * 4 bytes
	m_buffer_size = m_frames_per_update * wf.nBlockAlign;
	for (int i = 0; i < kNumBuffers; ++i) {
		m_buffers[i] = new BYTE[m_buffer_size];
		std::memset(m_buffers[i], 0, m_buffer_size);
	}

	HR(m_source->Start());
	m_current = 0;
	return S_OK;
}

BYTE* XAudio2Backend::GetNextBuffer()
{
	return m_buffers[m_current];
}

HRESULT XAudio2Backend::Submit(BYTE* buffer, DWORD bufferLength)
{
	if (!m_source) return E_FAIL;
	if (bufferLength == 0) return S_OK;

	// Check voice state to prevent overwriting data currently being played
	XAUDIO2_VOICE_STATE state;
	m_source->GetState(&state);

	// If we have too many buffers queued, the game loop is running too fast.
	// We should drop this frame or wait. For a game engine, dropping/skipping
	// update is usually better than stalling the main thread.
	if (state.BuffersQueued >= kNumBuffers - 1) {
		LOG_INFO("Audio warning: Ring buffer full, skipping update to prevent overwrite.");
		return S_OK;
	}

	BYTE* payload = buffer ? buffer : m_buffers[m_current];

	// Safety clamp
	if (!buffer && bufferLength > m_buffer_size) {
		bufferLength = (DWORD)m_buffer_size;
	}

	XAUDIO2_BUFFER xb = {};
	xb.AudioBytes = bufferLength;
	xb.pAudioData = payload;

	HRESULT hr = m_source->SubmitSourceBuffer(&xb);
	if (FAILED(hr)) {
		LOG_ERROR("XAudio2Backend::Submit: SubmitSourceBuffer failed, hr=0x%08X", (unsigned)hr);
		return hr;
	}

	// Only advance index if submission succeeded
	m_current = (m_current + 1) % kNumBuffers;
	return hr;
}

void XAudio2Backend::Shutdown()
{
	if (m_source) { m_source->DestroyVoice(); m_source = nullptr; }
	if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
	if (m_xaudio2) { m_xaudio2->Release();    m_xaudio2 = nullptr; }

	for (int i = 0; i < kNumBuffers; ++i) {
		delete[] m_buffers[i];
		m_buffers[i] = nullptr;
	}
	m_buffer_size = 0;
	m_current = 0;
	m_rate = 0;
	m_fps = 0;
	m_frames_per_update = 0;
	m_output_channels = 0;
	m_output_channel_mask = 0;

	if (m_com_init_local) { CoUninitialize(); m_com_init_local = false; }
}

void XAudio2Backend::SetMasterVolume(float linear)
{
	if (m_master) m_master->SetVolume(linear);
}

float XAudio2Backend::GetMasterVolume() const
{
	float v = 1.0f;
	if (m_master) m_master->GetVolume(&v);
	return v;
}
