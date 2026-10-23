#include "pch.h"
#include "PhoneSignIn.h"
#include "PsnAuth.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace psx
{
	namespace
	{
		constexpr wchar_t kPort[] = L"8642";

		std::string Page(std::string const &login_url, std::string const &message)
		{
			return "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
				   "<title>PS Xbox sign-in</title><style>"
				   "body{font-family:system-ui,sans-serif;background:#0e1015;color:#eee;margin:0;padding:24px;line-height:1.5}"
				   "a.b,button{display:block;width:100%;box-sizing:border-box;padding:14px;margin:12px 0;border:0;border-radius:8px;"
				   "background:#2f6fde;color:#fff;font-size:18px;text-align:center;text-decoration:none}"
				   "textarea{width:100%;box-sizing:border-box;height:96px;font-size:15px;border-radius:8px;padding:8px}"
				   "p.m{background:#1d3b2a;padding:12px;border-radius:8px}</style></head><body>"
				   "<h2>Sign in with PSN for PS Xbox</h2>" +
				(message.empty() ? "" : "<p class=\"m\">" + message + "</p>") +
				"<p>1. Sign in with the PSN account you use on the console:</p>"
				"<a class=\"b\" target=\"_blank\" href=\"" + login_url + "\">Sign in with PSN</a>"
				"<p>2. You land on a page from <b>remoteplay.dl.playstation.net</b> (it may look empty). Copy its address "
				"(long-press the address bar, then Copy), come back to this tab, and paste it here:</p>"
				"<form method=\"post\" action=\"/\"><textarea name=\"address\" placeholder=\"https://remoteplay.dl.playstation.net/remoteplay/redirect?code=...\"></textarea>"
				"<button type=\"submit\">Send to the Xbox</button></form></body></html>";
		}

		// Decodes a form field value (+ is a space).
		hstring FormValue(std::string const &body, std::string const &name)
		{
			std::string key = name + "=";
			size_t pos = body.find(key);
			if(pos == std::string::npos)
				return {};
			std::string value = body.substr(pos + key.size());
			value = value.substr(0, value.find('&'));
			std::replace(value.begin(), value.end(), '+', ' ');
			try
			{
				return Uri::UnescapeComponent(Widen(value));
			}
			catch(hresult_error const &)
			{
				return {};
			}
		}

		hstring LocalAddress()
		{
			for(auto const &host : NetworkInformation::GetHostNames())
			{
				if(host.Type() == HostNameType::Ipv4 && host.IPInformation())
					return host.CanonicalName();
			}
			return {};
		}
	}

	PhoneSignIn::~PhoneSignIn()
	{
		Stop();
	}

	IAsyncOperation<hstring> PhoneSignIn::StartAsync(std::function<void(hstring)> on_address)
	{
		Stop();
		on_address_ = std::move(on_address);
		hstring ip = LocalAddress();
		if(ip.empty())
			co_return hstring();

		StreamSocketListener listener;
		listener.ConnectionReceived([this](StreamSocketListener const &, StreamSocketListenerConnectionReceivedEventArgs const &args) {
			Serve(args.Socket());
		});
		try
		{
			co_await listener.BindServiceNameAsync(kPort);
		}
		catch(hresult_error const &e)
		{
			PSX_LOGE("Phone sign-in: can't listen on port %s: %s", Narrow(kPort).c_str(), Narrow(e.message()).c_str());
			co_return hstring();
		}
		listener_ = listener;
		hstring address = L"http://" + ip + L":" + kPort;
		PSX_LOGI("Phone sign-in page at %s", Narrow(address).c_str());
		co_return address;
	}

	void PhoneSignIn::Stop()
	{
		if(listener_)
		{
			listener_.Close();
			listener_ = nullptr;
		}
	}

	fire_and_forget PhoneSignIn::Serve(StreamSocket socket)
	{
		std::string request;
		try
		{
			// Read the headers, then the body (its length is in Content-Length).
			DataReader reader(socket.InputStream());
			reader.InputStreamOptions(InputStreamOptions::Partial);
			size_t body_len = 0, header_end = std::string::npos;
			while(request.size() < 65536)
			{
				uint32_t n = co_await reader.LoadAsync(8192);
				if(n == 0)
					break;
				std::vector<uint8_t> chunk(n);
				reader.ReadBytes(chunk);
				request.append(chunk.begin(), chunk.end());
				if(header_end == std::string::npos && (header_end = request.find("\r\n\r\n")) != std::string::npos)
				{
					std::string lower = request.substr(0, header_end);
					std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return static_cast<char>(tolower(static_cast<unsigned char>(c))); });
					size_t cl = lower.find("content-length:");
					body_len = cl == std::string::npos ? 0 : strtoul(lower.c_str() + cl + 15, nullptr, 10);
				}
				if(header_end != std::string::npos && request.size() >= header_end + 4 + body_len)
					break;
			}

			std::string message;
			if(request.compare(0, 5, "POST ") == 0 && header_end != std::string::npos)
			{
				hstring address = FormValue(request.substr(header_end + 4), "address");
				if(!address.empty() && std::wstring(address).find(L"code=") != std::wstring::npos)
				{
					message = "Sent. Check your Xbox: PS Xbox is finishing the sign-in.";
					if(on_address_)
						on_address_(address);
				}
				else
					message = "That's not the address Sony lands on: it must contain <b>code=</b>. Sign in again and copy the address of the page you land on.";
			}

			std::string login = Narrow(PsnLoginUri().AbsoluteUri());
			for(size_t p = 0; (p = login.find('&', p)) != std::string::npos; p += 5)
				login.replace(p, 1, "&amp;");
			std::string body = Page(login, message);
			std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
				std::to_string(body.size()) + "\r\n\r\n" + body;
			DataWriter writer(socket.OutputStream());
			writer.WriteBytes(array_view<const uint8_t>(reinterpret_cast<const uint8_t *>(response.data()),
				reinterpret_cast<const uint8_t *>(response.data()) + response.size()));
			co_await writer.StoreAsync();
			co_await writer.FlushAsync();
			writer.DetachStream();
		}
		catch(hresult_error const &e)
		{
			PSX_LOGE("Phone sign-in: %s", Narrow(e.message()).c_str());
		}
		socket.Close();
	}
}
