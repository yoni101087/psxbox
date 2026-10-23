#pragma once

#include "Settings.h"
#include "VideoDecoder.h"
#include "AudioPlayer.h"
#include "GamepadInput.h"

namespace psx
{
	class VideoRenderer;

	// One Remote Play session: chiaki session + video decoder + audio + controller.
	class StreamSession
	{
	public:
		struct Callbacks
		{
			std::function<void()> connected;                      // chiaki thread
			std::function<void(std::string const &reason)> ended; // chiaki or input thread
			std::function<void(bool on)> microphone;               // input thread: View + Y toggled it
		};

		StreamSession() = default;
		~StreamSession();

		bool Start(RegisteredHost const &host, std::string const &addr, StreamSettings const &settings,
			VideoRenderer *renderer, Callbacks callbacks);
		// Blocks until all threads have exited. UI thread.
		void Stop();
		bool Active() const { return active_; }

		// Interleaved 48 kHz stereo samples from the microphone (any thread). Sent while unmuted.
		void PushMicrophone(const int16_t *samples, size_t frames);

	private:
		static void EventCb(ChiakiEvent *event, void *user);
		static bool VideoCb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
		static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user);
		static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user);
		static void HapticsFrameCb(uint8_t *buf, size_t buf_size, void *user);
		void HapticsLoop();
		void ToggleMicrophone();
		void SetMicMuted(bool muted);

		ChiakiSession session_{};
		ChiakiOpusDecoder opus_{};
		VideoDecoder video_;
		AudioPlayer audio_;
		GamepadInput gamepad_;
		VideoRenderer *renderer_ = nullptr;
		Callbacks callbacks_;
		std::string addr_;
		bool active_ = false;
		std::atomic<bool> ended_sent_{ false };

		// PS5 games mostly vibrate through the DualSense's haptics, sent as audio. Its loudness is turned
		// into controller rumble, like chiaki-ng's rumble emulation (Normal intensity).
		std::mutex haptics_mutex_;
		std::deque<uint16_t> haptics_;
		std::thread haptics_thread_;
		std::atomic<bool> haptics_stop_{ false };

		// Microphone: chiaki encodes 10 ms frames of 480 stereo samples with Opus.
		static constexpr size_t kMicFrameSamples = 480;
		bool mic_enabled_ = false;
		std::atomic<bool> connected_{ false };
		std::mutex mic_mutex_;
		ChiakiOpusEncoder mic_encoder_{};
		bool mic_connected_ = false;
		bool mic_muted_ = true;
		std::vector<int16_t> mic_frame_;
	};
}
