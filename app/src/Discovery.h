#pragma once

namespace psx
{
	struct DiscoveredHost
	{
		ChiakiDiscoveryHostState state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
		std::string addr;
		std::string name;
		std::string mac;            // host_id, normalized to 12 uppercase hex chars
		std::string system_version;
		std::string running_app;
		bool ps5 = false;
		ChiakiTarget target = CHIAKI_TARGET_PS5_UNKNOWN;
	};

	// Periodically broadcasts discovery packets on the LAN (PS4 port 987, PS5 port 9302) and reports
	// which consoles answered.
	class Discovery
	{
	public:
		using Callback = std::function<void(std::vector<DiscoveredHost> const &)>;

		explicit Discovery(Callback cb);
		~Discovery();

		bool Start();
		void Stop();
		bool Running() const { return running_; }

		// Sends a wakeup packet to a console in rest mode.
		static bool Wakeup(std::string const &addr, std::array<uint8_t, CHIAKI_SESSION_AUTH_SIZE> const &regist_key, bool ps5);

	private:
		static void HostsCb(ChiakiDiscoveryHost *hosts, size_t count, void *user);
		static std::vector<uint32_t> BroadcastAddresses();

		Callback cb_;
		ChiakiDiscoveryService service_{};
		bool running_ = false;
	};
}
