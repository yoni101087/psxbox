#include "pch.h"
#include "PsnAuth.h"
#include "Logger.h"

#include <cerrno>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Json;
using namespace Windows::Security::Cryptography;
using namespace Windows::Web::Http;
using namespace Windows::Web::Http::Headers;

namespace psx
{
	namespace
	{
		// Remote Play Windows client credentials, as used by chiaki-ng (scripts/psn-account-id.py).
		constexpr wchar_t kClientId[] = L"ba495a24-818c-472b-b12d-ff231c1b5745";
		constexpr wchar_t kClientSecret[] = L"mvaiZkRsAsI1IBkY";
		constexpr wchar_t kRedirectUri[] = L"https://remoteplay.dl.playstation.net/remoteplay/redirect";
		constexpr wchar_t kTokenUrl[] = L"https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";

		hstring LoginUrl()
		{
			return hstring(L"https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"
				L"?service_entity=urn:service-entity:psn&response_type=code&client_id=") + kClientId +
				L"&redirect_uri=" + kRedirectUri +
				L"&scope=psn:clientapp&request_locale=en_US&ui=pr&service_logo=ps&layout_type=popup"
				L"&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&";
		}

		HttpCredentialsHeaderValue BasicAuth()
		{
			auto buf = CryptographicBuffer::ConvertStringToBinary(hstring(kClientId) + L":" + kClientSecret, BinaryStringEncoding::Utf8);
			return HttpCredentialsHeaderValue(L"Basic", CryptographicBuffer::EncodeToBase64String(buf));
		}

		std::string ToBase64(uint64_t id)
		{
			uint8_t bytes[8];
			for(int i = 0; i < 8; i++)
				bytes[i] = static_cast<uint8_t>(id >> (8 * i));
			auto buf = CryptographicBuffer::CreateFromByteArray(array_view<const uint8_t>(bytes));
			return Narrow(CryptographicBuffer::EncodeToBase64String(buf));
		}

		[[noreturn]] void Fail(std::string const &msg)
		{
			PSX_LOGE("PSN sign-in: %s", msg.c_str());
			throw hresult_error(E_FAIL, Widen(msg));
		}

		// The code itself, from the code or the whole redirect address (...redirect?code=XXXX&...).
		std::string ExtractCode(std::string input)
		{
			while(!input.empty() && isspace(static_cast<unsigned char>(input.back())))
				input.pop_back();
			size_t start = 0;
			while(start < input.size() && isspace(static_cast<unsigned char>(input[start])))
				start++;
			input = input.substr(start);

			size_t pos = input.find("code=");
			if(pos == std::string::npos)
				return input;
			std::string code = input.substr(pos + 5);
			size_t end = code.find_first_of("&#");
			if(end != std::string::npos)
				code.resize(end);
			return Narrow(Uri::UnescapeComponent(Widen(code)));
		}
	}

	Uri PsnLoginUri()
	{
		return Uri(LoginUrl());
	}

	IAsyncOperation<hstring> PsnAccountIdFromCodeAsync(hstring input)
	{
		std::string code = ExtractCode(Narrow(input));
		if(code.empty())
			Fail("enter the code from Edge's address bar");

		HttpClient client;
		client.DefaultRequestHeaders().Authorization(BasicAuth());

		HttpStringContent body(hstring(L"grant_type=authorization_code&code=") + Uri::EscapeComponent(Widen(code)) + L"&redirect_uri=" + kRedirectUri + L"&",
			Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/x-www-form-urlencoded");
		auto token_response = co_await client.PostAsync(Uri(kTokenUrl), body);
		auto token_text = co_await token_response.Content().ReadAsStringAsync();
		if(!token_response.IsSuccessStatusCode())
			Fail(Format("Sony didn't accept the code (HTTP %d). Codes expire after a few minutes: sign in again.",
				static_cast<int>(token_response.StatusCode())));
		JsonObject token_json;
		if(!JsonObject::TryParse(token_text, token_json) || !token_json.HasKey(L"access_token"))
			Fail("token response had no access token");
		hstring token = token_json.GetNamedString(L"access_token");

		auto info_response = co_await client.GetAsync(Uri(hstring(kTokenUrl) + L"/" + Uri::EscapeComponent(token)));
		auto info_text = co_await info_response.Content().ReadAsStringAsync();
		if(!info_response.IsSuccessStatusCode())
			Fail(Format("account info request failed (HTTP %d)", static_cast<int>(info_response.StatusCode())));
		JsonObject info_json;
		if(!JsonObject::TryParse(info_text, info_json) || !info_json.HasKey(L"user_id"))
			Fail("account info had no user id");

		// user_id is a decimal number, sent as a string.
		auto value = info_json.GetNamedValue(L"user_id");
		std::string user_id = value.ValueType() == JsonValueType::String ? Narrow(value.GetString())
			: Format("%.0f", value.GetNumber());
		std::string id = NormalizeAccountId(user_id);
		if(id.empty())
			Fail("unexpected user id format");
		PSX_LOGI("PSN sign-in succeeded");
		co_return Widen(id);
	}

	std::string NormalizeAccountId(std::string const &input)
	{
		if(input.empty())
			return {};

		if(std::all_of(input.begin(), input.end(), [](char c) { return c >= '0' && c <= '9'; }))
		{
			if(input.size() > 20)
				return {};
			errno = 0;
			unsigned long long id = strtoull(input.c_str(), nullptr, 10);
			if(errno == ERANGE || id == 0)
				return {};
			return ToBase64(id);
		}

		uint8_t bytes[CHIAKI_PSN_ACCOUNT_ID_SIZE];
		size_t size = sizeof(bytes);
		if(chiaki_base64_decode(input.c_str(), input.size(), bytes, &size) == CHIAKI_ERR_SUCCESS && size == CHIAKI_PSN_ACCOUNT_ID_SIZE)
			return input;
		return {};
	}
}
