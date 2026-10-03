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
		};

		StreamSession() = default;
		~StreamSession();

		bool Start(RegisteredHost const &host, std::string const &addr, StreamSettings const &settings,
			VideoRenderer *renderer, Callbacks callbacks);
		// Blocks until all threads have exited. UI thread.
		void Stop();
		bool Active() const { return active_; }

	private:
		static void EventCb(ChiakiEvent *event, void *user);
		static bool VideoCb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
		static void AudioSettingsCb(uint32_t channels, uint32_t rate, void *user);
		static void AudioFrameCb(int16_t *buf, size_t samples_count, void *user);

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
	};
}
