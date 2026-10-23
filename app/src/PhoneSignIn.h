#pragma once

namespace psx
{
	// Sign in with PSN from a phone (or PC) on the same network, for when the page on the Xbox can't be
	// used. Serves a small page at http://<xbox-ip>:<port>/ that links to Sony's sign-in; after signing
	// in, the user pastes the address Sony lands on into that page, and it is handed to on_address.
	class PhoneSignIn
	{
	public:
		PhoneSignIn() = default;
		~PhoneSignIn();

		// UI thread. Returns the address to open on the phone, or empty if the server couldn't start.
		// on_address is called (on a network thread) with the pasted address.
		winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> StartAsync(std::function<void(winrt::hstring)> on_address);
		void Stop();

	private:
		winrt::fire_and_forget Serve(winrt::Windows::Networking::Sockets::StreamSocket socket);

		winrt::Windows::Networking::Sockets::StreamSocketListener listener_{ nullptr };
		std::function<void(winrt::hstring)> on_address_;
	};
}
