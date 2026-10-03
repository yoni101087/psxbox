#include "pch.h"
#include "Registration.h"
#include "Logger.h"

namespace psx
{
	Registration::~Registration()
	{
		Stop();
	}

	bool Registration::Start(std::string const &addr, ChiakiTarget target, std::string const &psn_account_id_b64, uint32_t pin, Callback cb)
	{
		Stop();

		uint8_t account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
		size_t account_id_size = sizeof(account_id);
		if(chiaki_base64_decode(psn_account_id_b64.c_str(), psn_account_id_b64.size(), account_id, &account_id_size) != CHIAKI_ERR_SUCCESS
			|| account_id_size != CHIAKI_PSN_ACCOUNT_ID_SIZE)
		{
			PSX_LOGE("PSN Account ID must be 8 bytes encoded as base64 (12 characters, usually ending in '=')");
			return false;
		}

		addr_ = addr;
		cb_ = std::move(cb);

		ChiakiRegistInfo info = {};
		info.host = addr_.c_str();
		info.target = target;
		info.broadcast = false;
		info.psn_online_id = nullptr;
		memcpy(info.psn_account_id, account_id, sizeof(account_id));
		info.pin = pin;
		info.console_pin = 0;
		info.holepunch_info = nullptr;
		info.rudp = nullptr;

		PSX_LOGI("Registering with %s (%s)...", addr_.c_str(), chiaki_target_is_ps5(target) ? "PS5" : "PS4");
		ChiakiErrorCode err = chiaki_regist_start(&regist_, Logger::Get().Chiaki(), &info, RegistCb, this);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			PSX_LOGE("Failed to start registration: %s", chiaki_error_string(err));
			return false;
		}
		active_ = true;
		return true;
	}

	void Registration::Stop()
	{
		if(!active_)
			return;
		chiaki_regist_stop(&regist_);
		chiaki_regist_fini(&regist_);
		active_ = false;
	}

	void Registration::RegistCb(ChiakiRegistEvent *event, void *user)
	{
		auto self = static_cast<Registration *>(user);
		std::optional<RegisteredHost> result;
		switch(event->type)
		{
			case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
			{
				ChiakiRegisteredHost *rh = event->registered_host;
				RegisteredHost h;
				h.mac = MacToString(rh->server_mac);
				h.nickname = std::string(rh->server_nickname, strnlen(rh->server_nickname, sizeof(rh->server_nickname)));
				h.target = rh->target;
				memcpy(h.regist_key.data(), rh->rp_regist_key, h.regist_key.size());
				memcpy(h.rp_key.data(), rh->rp_key, h.rp_key.size());
				h.last_addr = self->addr_;
				PSX_LOGI("Registered with %s (%s)", h.nickname.c_str(), h.mac.c_str());
				result = h;
				break;
			}
			case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
				PSX_LOGE("Registration failed. Check the PIN (it expires after a few minutes) and the PSN Account ID.");
				break;
			case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
				PSX_LOGI("Registration canceled");
				break;
		}
		if(self->cb_)
			self->cb_(result);
	}
}
