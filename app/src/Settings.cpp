#include "pch.h"
#include "Settings.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Security::Cryptography;

namespace psx
{
	namespace
	{
		constexpr wchar_t kHostsContainer[] = L"hosts";

		ApplicationDataContainer Root()
		{
			return ApplicationData::Current().LocalSettings();
		}

		template<size_t N>
		hstring ToBase64(std::array<uint8_t, N> const &data)
		{
			auto buf = CryptographicBuffer::CreateFromByteArray(array_view<const uint8_t>(data.data(), data.data() + N));
			return CryptographicBuffer::EncodeToBase64String(buf);
		}

		template<size_t N>
		bool FromBase64(hstring const &s, std::array<uint8_t, N> &out)
		{
			try
			{
				auto buf = CryptographicBuffer::DecodeFromBase64String(s);
				com_array<uint8_t> bytes;
				CryptographicBuffer::CopyToByteArray(buf, bytes);
				if(bytes.size() != N)
					return false;
				std::copy(bytes.begin(), bytes.end(), out.begin());
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

		hstring GetString(ApplicationDataCompositeValue const &v, wchar_t const *key)
		{
			auto o = v.TryLookup(key);
			return o ? unbox_value_or<hstring>(o, L"") : hstring();
		}

		int32_t GetInt(IPropertySet const &v, wchar_t const *key, int32_t def)
		{
			auto o = v.TryLookup(key);
			return o ? unbox_value_or<int32_t>(o, def) : def;
		}
	}

	std::string MacToString(const uint8_t mac[6])
	{
		return Format("%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	std::string NormalizeMac(std::string const &s)
	{
		std::string r;
		for(char c : s)
		{
			if(isxdigit(static_cast<unsigned char>(c)))
				r.push_back(static_cast<char>(toupper(static_cast<unsigned char>(c))));
		}
		return r;
	}

	Settings &Settings::Get()
	{
		static Settings instance;
		return instance;
	}

	Settings::Settings()
	{
		Load();
	}

	void Settings::Load()
	{
		auto root = Root();
		auto values = root.Values();

		psn_account_id_ = Narrow(unbox_value_or<hstring>(values.TryLookup(L"psn_account_id"), L""));
		stream_.resolution = static_cast<ChiakiVideoResolutionPreset>(GetInt(values, L"resolution", CHIAKI_VIDEO_RESOLUTION_PRESET_1080p));
		stream_.fps = static_cast<ChiakiVideoFPSPreset>(GetInt(values, L"fps", CHIAKI_VIDEO_FPS_PRESET_60));
		stream_.codec = static_cast<ChiakiCodec>(GetInt(values, L"codec", CHIAKI_CODEC_H264));
		stream_.bitrate_kbps = static_cast<unsigned int>(GetInt(values, L"bitrate_kbps", 0));
		stream_.ps5_layout = GetInt(values, L"ps5_layout", 0) != 0;
		stream_.microphone = GetInt(values, L"microphone", 0) != 0;

		std::string manual = Narrow(unbox_value_or<hstring>(values.TryLookup(L"manual_addrs"), L""));
		size_t pos = 0;
		while(pos < manual.size())
		{
			size_t end = manual.find(';', pos);
			if(end == std::string::npos)
				end = manual.size();
			if(end > pos)
				manual_addrs_.push_back(manual.substr(pos, end - pos));
			pos = end + 1;
		}

		hosts_.clear();
		auto containers = root.Containers();
		if(!containers.HasKey(kHostsContainer))
			return;
		for(auto const &kv : containers.Lookup(kHostsContainer).Values())
		{
			auto v = kv.Value().try_as<ApplicationDataCompositeValue>();
			if(!v)
				continue;
			RegisteredHost h;
			h.mac = Narrow(kv.Key());
			h.nickname = Narrow(GetString(v, L"nickname"));
			h.target = static_cast<ChiakiTarget>(GetInt(v, L"target", CHIAKI_TARGET_PS5_1));
			h.last_addr = Narrow(GetString(v, L"addr"));
			if(!FromBase64(GetString(v, L"regist_key"), h.regist_key) || !FromBase64(GetString(v, L"rp_key"), h.rp_key))
			{
				PSX_LOGE("Ignoring corrupt saved console %s", h.mac.c_str());
				continue;
			}
			hosts_.push_back(std::move(h));
		}
	}

	void Settings::SaveHosts()
	{
		auto root = Root();
		root.DeleteContainer(kHostsContainer);
		auto c = root.CreateContainer(kHostsContainer, ApplicationDataCreateDisposition::Always);
		for(auto const &h : hosts_)
		{
			ApplicationDataCompositeValue v;
			v.Insert(L"nickname", box_value(Widen(h.nickname)));
			v.Insert(L"target", box_value(static_cast<int32_t>(h.target)));
			v.Insert(L"addr", box_value(Widen(h.last_addr)));
			v.Insert(L"regist_key", box_value(ToBase64(h.regist_key)));
			v.Insert(L"rp_key", box_value(ToBase64(h.rp_key)));
			c.Values().Insert(Widen(h.mac), v);
		}
	}

	void Settings::SaveMisc()
	{
		auto values = Root().Values();
		values.Insert(L"psn_account_id", box_value(Widen(psn_account_id_)));
		values.Insert(L"resolution", box_value(static_cast<int32_t>(stream_.resolution)));
		values.Insert(L"fps", box_value(static_cast<int32_t>(stream_.fps)));
		values.Insert(L"codec", box_value(static_cast<int32_t>(stream_.codec)));
		values.Insert(L"bitrate_kbps", box_value(static_cast<int32_t>(stream_.bitrate_kbps)));
		values.Insert(L"ps5_layout", box_value(static_cast<int32_t>(stream_.ps5_layout ? 1 : 0)));
		values.Insert(L"microphone", box_value(static_cast<int32_t>(stream_.microphone ? 1 : 0)));
		std::string manual;
		for(auto const &a : manual_addrs_)
			manual += a + ";";
		values.Insert(L"manual_addrs", box_value(Widen(manual)));
	}

	RegisteredHost const *Settings::FindHost(std::string const &mac) const
	{
		std::string m = NormalizeMac(mac);
		for(auto const &h : hosts_)
		{
			if(h.mac == m)
				return &h;
		}
		return nullptr;
	}

	void Settings::AddOrUpdateHost(RegisteredHost const &host)
	{
		RegisteredHost h = host;
		h.mac = NormalizeMac(h.mac);
		auto it = std::find_if(hosts_.begin(), hosts_.end(), [&](auto const &x) { return x.mac == h.mac; });
		if(it != hosts_.end())
			*it = h;
		else
			hosts_.push_back(h);
		SaveHosts();
	}

	void Settings::RemoveHost(std::string const &mac)
	{
		std::string m = NormalizeMac(mac);
		hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(), [&](auto const &x) { return x.mac == m; }), hosts_.end());
		SaveHosts();
	}

	void Settings::SetPsnAccountId(std::string const &id)
	{
		psn_account_id_ = id;
		SaveMisc();
	}

	void Settings::AddManualAddress(std::string const &addr)
	{
		if(std::find(manual_addrs_.begin(), manual_addrs_.end(), addr) == manual_addrs_.end())
			manual_addrs_.push_back(addr);
		SaveMisc();
	}

	void Settings::SetStream(StreamSettings const &s)
	{
		stream_ = s;
		SaveMisc();
	}
}
