#pragma once

namespace psx
{
	// Records the default communications microphone (a headset on the controller) with AudioGraph, as
	// 48 kHz stereo 16-bit samples.
	class MicCapture
	{
	public:
		// Called on an audio thread with interleaved stereo samples.
		using FrameCallback = std::function<void(const int16_t *samples, size_t frames)>;

		MicCapture() = default;
		~MicCapture();

		// UI thread, so the Xbox can ask for permission to use the microphone.
		winrt::Windows::Foundation::IAsyncOperation<bool> StartAsync(FrameCallback cb);
		void Stop();

	private:
		winrt::Windows::Media::Audio::AudioGraph graph_{ nullptr };
		winrt::Windows::Media::Audio::AudioFrameOutputNode output_{ nullptr };
		FrameCallback cb_;
		std::vector<int16_t> pcm_;
	};
}
