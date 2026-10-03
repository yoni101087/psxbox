#include "pch.h"
#include "AudioPlayer.h"
#include "Logger.h"

namespace psx
{
	AudioPlayer::~AudioPlayer()
	{
		Fini();
	}

	bool AudioPlayer::Init()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(xaudio_)
			return true;
		HRESULT hr = XAudio2Create(xaudio_.put(), 0, XAUDIO2_DEFAULT_PROCESSOR);
		if(SUCCEEDED(hr))
			hr = xaudio_->CreateMasteringVoice(&master_);
		if(FAILED(hr))
		{
			PSX_LOGE("XAudio2 init failed: 0x%08x", hr);
			xaudio_ = nullptr;
			return false;
		}
		return true;
	}

	void AudioPlayer::Fini()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(source_)
		{
			source_->Stop();
			source_->FlushSourceBuffers();
			source_->DestroyVoice();
			source_ = nullptr;
		}
		if(master_)
		{
			master_->DestroyVoice();
			master_ = nullptr;
		}
		xaudio_ = nullptr;
		channels_ = rate_ = 0;
	}

	void AudioPlayer::Configure(uint32_t channels, uint32_t rate)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(!xaudio_ || (source_ && channels == channels_ && rate == rate_))
			return;
		if(source_)
		{
			source_->DestroyVoice();
			source_ = nullptr;
		}

		WAVEFORMATEX wfx = {};
		wfx.wFormatTag = WAVE_FORMAT_PCM;
		wfx.nChannels = static_cast<WORD>(channels);
		wfx.nSamplesPerSec = rate;
		wfx.wBitsPerSample = 16;
		wfx.nBlockAlign = static_cast<WORD>(channels * 2);
		wfx.nAvgBytesPerSec = rate * wfx.nBlockAlign;
		HRESULT hr = xaudio_->CreateSourceVoice(&source_, &wfx);
		if(FAILED(hr))
		{
			PSX_LOGE("CreateSourceVoice failed: 0x%08x", hr);
			source_ = nullptr;
			return;
		}
		source_->Start();
		channels_ = channels;
		rate_ = rate;
		PSX_LOGI("Audio: %u channels @ %u Hz", channels, rate);
	}

	void AudioPlayer::Push(const int16_t *pcm, size_t frames)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(!source_ || frames == 0)
			return;

		XAUDIO2_VOICE_STATE state = {};
		source_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if(state.BuffersQueued >= kMaxQueued)
			return; // behind: drop this packet

		// Buffers must stay valid until XAudio2 has consumed them; the pool is larger than kMaxQueued.
		auto &buf = pool_[pool_index_];
		pool_index_ = (pool_index_ + 1) % kPoolSize;
		buf.assign(pcm, pcm + frames * channels_);

		XAUDIO2_BUFFER xb = {};
		xb.AudioBytes = static_cast<UINT32>(buf.size() * sizeof(int16_t));
		xb.pAudioData = reinterpret_cast<const BYTE *>(buf.data());
		source_->SubmitSourceBuffer(&xb);
	}
}
