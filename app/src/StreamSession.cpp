#include "pch.h"
#include "StreamSession.h"
#include "VideoRenderer.h"
#include "Logger.h"

namespace psx
{
	StreamSession::~StreamSession()
	{
		Stop();
	}

	bool StreamSession::Start(RegisteredHost const &host, std::string const &addr, StreamSettings const &settings,
		VideoRenderer *renderer, Callbacks callbacks)
	{
		Stop();
		renderer_ = renderer;
		callbacks_ = std::move(callbacks);
		addr_ = addr;
		ended_sent_ = false;

		ChiakiConnectInfo ci = {};
		ci.ps5 = host.IsPs5();
		ci.host = addr_.c_str();
		memcpy(ci.regist_key, host.regist_key.data(), sizeof(ci.regist_key));
		memcpy(ci.morning, host.rp_key.data(), sizeof(ci.morning));
		chiaki_connect_video_profile_preset(&ci.video_profile, settings.resolution, settings.fps);
		if(settings.bitrate_kbps)
			ci.video_profile.bitrate = settings.bitrate_kbps;
		// H.265 (and its HDR variant) is PS5 only.
		ci.video_profile.codec = ci.ps5 && chiaki_codec_is_h265(settings.codec) ? settings.codec : CHIAKI_CODEC_H264;
		ci.video_profile_auto_downgrade = true;
		ci.enable_keyboard = false;
		ci.enable_dualsense = false;
		ci.audio_video_disabled = CHIAKI_NONE_DISABLED;
		ci.auto_regist = false;
		ci.holepunch_session = nullptr;
		ci.rudp_sock = nullptr;
		ci.packet_loss_max = 0.05;
		ci.enable_idr_on_fec_failure = true;

		PSX_LOGI("Connecting to %s at %s: %ux%u@%u %s %u kbps", host.nickname.c_str(), addr_.c_str(),
			ci.video_profile.width, ci.video_profile.height, ci.video_profile.max_fps,
			chiaki_codec_name(ci.video_profile.codec), ci.video_profile.bitrate);

		if(!video_.Init(ci.video_profile.codec, ci.video_profile.width, ci.video_profile.height, renderer_))
			return false;
		audio_.Init();

		ChiakiErrorCode err = chiaki_session_init(&session_, &ci, Logger::Get().Chiaki());
		if(err != CHIAKI_ERR_SUCCESS)
		{
			PSX_LOGE("Session init failed: %s", chiaki_error_string(err));
			video_.Fini();
			audio_.Fini();
			return false;
		}

		chiaki_opus_decoder_init(&opus_, Logger::Get().Chiaki());
		chiaki_opus_decoder_set_cb(&opus_, AudioSettingsCb, AudioFrameCb, this);
		ChiakiAudioSink sink;
		chiaki_opus_decoder_get_sink(&opus_, &sink);
		chiaki_session_set_audio_sink(&session_, &sink);
		chiaki_session_set_video_sample_cb(&session_, VideoCb, this);
		chiaki_session_set_event_cb(&session_, EventCb, this);

		renderer_->StartThread();

		err = chiaki_session_start(&session_);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			PSX_LOGE("Session start failed: %s", chiaki_error_string(err));
			renderer_->StopThread();
			chiaki_session_fini(&session_);
			chiaki_opus_decoder_fini(&opus_);
			video_.Fini();
			audio_.Fini();
			return false;
		}
		active_ = true;

		gamepad_.Start(
			[this](ChiakiControllerState &s) { chiaki_session_set_controller_state(&session_, &s); },
			[this] {
				PSX_LOGI("Quit combo pressed");
				if(!ended_sent_.exchange(true) && callbacks_.ended)
					callbacks_.ended("");
			},
			settings.ps5_layout);
		return true;
	}

	void StreamSession::Stop()
	{
		if(!active_)
			return;
		gamepad_.Stop();
		chiaki_session_stop(&session_);
		chiaki_session_join(&session_);
		chiaki_session_fini(&session_);
		chiaki_opus_decoder_fini(&opus_);
		renderer_->StopThread();
		video_.Fini();
		audio_.Fini();
		active_ = false;
		PSX_LOGI("Session closed");
	}

	void StreamSession::EventCb(ChiakiEvent *event, void *user)
	{
		auto self = static_cast<StreamSession *>(user);
		switch(event->type)
		{
			case CHIAKI_EVENT_CONNECTED:
				PSX_LOGI("Connected");
				if(self->callbacks_.connected)
					self->callbacks_.connected();
				break;
			case CHIAKI_EVENT_QUIT:
			{
				std::string reason = chiaki_quit_reason_string(event->quit.reason);
				if(event->quit.reason_str)
					reason += std::string(": ") + event->quit.reason_str;
				if(!chiaki_quit_reason_is_error(event->quit.reason))
					reason.clear();
				PSX_LOGI("Session ended (%s)", chiaki_quit_reason_string(event->quit.reason));
				if(!self->ended_sent_.exchange(true) && self->callbacks_.ended)
					self->callbacks_.ended(reason);
				break;
			}
			case CHIAKI_EVENT_RUMBLE:
				self->gamepad_.Rumble(event->rumble.left, event->rumble.right);
				break;
			case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
				PSX_LOGE("The console asks for a login passcode. Remove the passcode from your PlayStation user (not supported yet).");
				chiaki_session_stop(&self->session_);
				break;
			default:
				break;
		}
	}

	bool StreamSession::VideoCb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
	{
		(void)frames_lost;
		(void)frame_recovered;
		auto self = static_cast<StreamSession *>(user);
		return self->video_.Decode(buf, buf_size);
	}

	void StreamSession::AudioSettingsCb(uint32_t channels, uint32_t rate, void *user)
	{
		static_cast<StreamSession *>(user)->audio_.Configure(channels, rate);
	}

	void StreamSession::AudioFrameCb(int16_t *buf, size_t samples_count, void *user)
	{
		static_cast<StreamSession *>(user)->audio_.Push(buf, samples_count);
	}
}
