#include "pch.h"
#include "Discovery.h"
#include "Logger.h"
#include "Settings.h"

using namespace winrt;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;

namespace psx
{
	static constexpr uint64_t kPingMs = 500;
	static constexpr uint64_t kDropPings = 3;
	static constexpr size_t kHostsMax = 16;

	Discovery::Discovery(Callback cb) : cb_(std::move(cb))
	{
	}

	Discovery::~Discovery()
	{
		Stop();
	}

	// Subnet broadcast addresses of all local IPv4 interfaces, plus manually added console addresses.
	// Some routers drop 255.255.255.255, so sending to x.x.x.255 as well makes discovery more reliable.
	std::vector<uint32_t> Discovery::BroadcastAddresses()
	{
		std::vector<uint32_t> result;
		auto add = [&](uint32_t a) {
			if(std::find(result.begin(), result.end(), a) == result.end())
				result.push_back(a);
		};

		try
		{
			for(auto const &hn : NetworkInformation::GetHostNames())
			{
				if(hn.Type() != HostNameType::Ipv4 || !hn.IPInformation())
					continue;
				auto prefix_ref = hn.IPInformation().PrefixLength();
				if(!prefix_ref)
					continue;
				uint8_t prefix = prefix_ref.Value();
				in_addr addr{};
				if(inet_pton(AF_INET, Narrow(hn.CanonicalName()).c_str(), &addr) != 1)
					continue;
				uint32_t ip = ntohl(addr.s_addr);
				uint32_t mask = prefix == 0 ? 0 : (0xffffffffu << (32 - prefix));
				add(htonl(ip | ~mask));
			}
		}
		catch(...)
		{
			PSX_LOGE("Failed to enumerate network interfaces");
		}

		for(auto const &manual : Settings::Get().ManualAddresses())
		{
			in_addr addr{};
			if(inet_pton(AF_INET, manual.c_str(), &addr) == 1)
				add(addr.s_addr);
		}
		return result;
	}

	bool Discovery::Start()
	{
		if(running_)
			return true;

		ChiakiDiscoveryServiceOptions options = {};
		options.ping_ms = kPingMs;
		options.ping_initial_ms = kPingMs;
		options.hosts_max = kHostsMax;
		options.host_drop_pings = kDropPings;
		options.cb = HostsCb;
		options.cb_user = this;

		sockaddr_in in = {};
		in.sin_family = AF_INET;
		in.sin_addr.s_addr = 0xffffffff;
		sockaddr_storage send_addr = {};
		memcpy(&send_addr, &in, sizeof(in));
		options.send_addr = &send_addr;
		options.send_addr_size = sizeof(in);

		std::vector<sockaddr_storage> bcast;
		for(uint32_t a : BroadcastAddresses())
		{
			sockaddr_in b = {};
			b.sin_family = AF_INET;
			b.sin_addr.s_addr = a;
			sockaddr_storage s = {};
			memcpy(&s, &b, sizeof(b));
			bcast.push_back(s);
			char str[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &b.sin_addr, str, sizeof(str));
			PSX_LOGI("Discovery: also probing %s", str);
		}
		options.broadcast_addrs = bcast.empty() ? nullptr : bcast.data();
		options.broadcast_num = bcast.size();

		ChiakiErrorCode err = chiaki_discovery_service_init(&service_, &options, Logger::Get().Chiaki());
		if(err != CHIAKI_ERR_SUCCESS)
		{
			PSX_LOGE("Discovery failed to start: %s", chiaki_error_string(err));
			return false;
		}
		running_ = true;
		return true;
	}

	void Discovery::Stop()
	{
		if(!running_)
			return;
		chiaki_discovery_service_fini(&service_);
		running_ = false;
	}

	void Discovery::HostsCb(ChiakiDiscoveryHost *hosts, size_t count, void *user)
	{
		auto self = static_cast<Discovery *>(user);
		std::vector<DiscoveredHost> result;
		for(size_t i = 0; i < count; i++)
		{
			ChiakiDiscoveryHost *h = &hosts[i];
			DiscoveredHost d;
			d.state = h->state;
			d.addr = h->host_addr ? h->host_addr : "";
			d.name = h->host_name ? h->host_name : "";
			d.mac = NormalizeMac(h->host_id ? h->host_id : "");
			d.system_version = h->system_version ? h->system_version : "";
			d.running_app = h->running_app_name ? h->running_app_name : "";
			d.ps5 = chiaki_discovery_host_is_ps5(h);
			d.target = chiaki_discovery_host_system_version_target(h);
			result.push_back(std::move(d));
		}
		if(self->cb_)
			self->cb_(result);
	}

	bool Discovery::Wakeup(std::string const &addr, std::array<uint8_t, CHIAKI_SESSION_AUTH_SIZE> const &regist_key, bool ps5)
	{
		// The wakeup credential is the regist key (an ASCII hex string, \0 padded) parsed as a number.
		std::string key;
		for(uint8_t c : regist_key)
		{
			if(!c)
				break;
			key.push_back(static_cast<char>(c));
		}
		if(key.empty() || key.size() > 8)
		{
			PSX_LOGE("Invalid regist key for wakeup");
			return false;
		}
		uint64_t credential = strtoull(key.c_str(), nullptr, 16);
		ChiakiErrorCode err = chiaki_discovery_wakeup(Logger::Get().Chiaki(), nullptr, addr.c_str(), credential, ps5);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			PSX_LOGE("Wakeup failed: %s", chiaki_error_string(err));
			return false;
		}
		PSX_LOGI("Sent wakeup to %s", addr.c_str());
		return true;
	}
}
