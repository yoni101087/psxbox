#pragma once

namespace psx
{
	// Plays the PCM coming out of chiaki's Opus decoder through XAudio2.
	// Keeps the queue short (drops audio when it lags) so latency doesn't build up.
	class AudioPlayer
	{
	public:
		AudioPlayer() = default;
		~AudioPlayer();

		bool Init();
		void Fini();

		// Both called on chiaki's audio thread.
		void Configure(uint32_t channels, uint32_t rate);
		void Push(const int16_t *pcm, size_t frames);

	private:
		static constexpr size_t kPoolSize = 16;
		static constexpr UINT32 kMaxQueued = 6;

		std::mutex mutex_;
		winrt::com_ptr<IXAudio2> xaudio_;
		IXAudio2MasteringVoice *master_ = nullptr;
		IXAudio2SourceVoice *source_ = nullptr;
		uint32_t channels_ = 0;
		uint32_t rate_ = 0;
		std::array<std::vector<int16_t>, kPoolSize> pool_;
		size_t pool_index_ = 0;
	};
}
