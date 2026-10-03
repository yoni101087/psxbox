#pragma once

#include "Settings.h"

namespace psx
{
	// Pairs this app with a console using the 8-digit PIN shown under
	// Settings > System > Remote Play > Link Device (PS5) / Add Device (PS4).
	class Registration
	{
	public:
		// Called once on a chiaki thread: success with the host, or failure (host == nullopt).
		using Callback = std::function<void(std::optional<RegisteredHost> const &)>;

		Registration() = default;
		~Registration();

		// psn_account_id_b64: base64 of the 8-byte PSN account id.
		bool Start(std::string const &addr, ChiakiTarget target, std::string const &psn_account_id_b64, uint32_t pin, Callback cb);
		void Stop();

	private:
		static void RegistCb(ChiakiRegistEvent *event, void *user);

		ChiakiRegist regist_{};
		bool active_ = false;
		std::string addr_;
		Callback cb_;
	};
}
