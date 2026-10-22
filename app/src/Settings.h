#pragma once

namespace psx
{
	// A console this Xbox has been paired with (result of a successful registration).
	struct RegisteredHost
	{
		std::string mac;          // 12 uppercase hex chars, matches the discovery host_id
		std::string nickname;
		ChiakiTarget target = CHIAKI_TARGET_PS5_1;
		std::array<uint8_t, CHIAKI_SESSION_AUTH_SIZE> regist_key{}; // rp_regist_key
		std::array<uint8_t, 0x10> rp_key{};                        // "morning"
		std::string last_addr;

		bool IsPs5() const { return chiaki_target_is_ps5(target); }
	};

	struct StreamSettings
	{
		ChiakiVideoResolutionPreset resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
		ChiakiVideoFPSPreset fps = CHIAKI_VIDEO_FPS_PRESET_60;
		ChiakiCodec codec = CHIAKI_CODEC_H264;
		unsigned int bitrate_kbps = 0; // 0 = preset default
		// A PS5 controller through an adapter (such as the Brook Wingman XB3), which turns Create into View.
		bool ps5_layout = false;
	};

	// Persists everything in ApplicationData.LocalSettings.
	class Settings
	{
	public:
		static Settings &Get();

		std::vector<RegisteredHost> const &Hosts() const { return hosts_; }
		RegisteredHost const *FindHost(std::string const &mac) const;
		void AddOrUpdateHost(RegisteredHost const &host);
		void RemoveHost(std::string const &mac);

		// Base64 PSN account id (8 bytes), required for registering PS4 >= 7.0 and PS5.
		std::string PsnAccountId() const { return psn_account_id_; }
		void SetPsnAccountId(std::string const &id);

		// Extra addresses to probe (consoles on another subnet / broadcast not reaching them).
		std::vector<std::string> const &ManualAddresses() const { return manual_addrs_; }
		void AddManualAddress(std::string const &addr);

		StreamSettings Stream() const { return stream_; }
		void SetStream(StreamSettings const &s);

	private:
		Settings();
		void Load();
		void SaveHosts();
		void SaveMisc();

		std::vector<RegisteredHost> hosts_;
		std::string psn_account_id_;
		std::vector<std::string> manual_addrs_;
		StreamSettings stream_;
	};

	std::string MacToString(const uint8_t mac[6]);
	std::string NormalizeMac(std::string const &s);
}
