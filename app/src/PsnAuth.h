#pragma once

namespace psx
{
	// PSN sign-in, the same way chiaki-ng's psn-account-id.py does: the user signs in to Sony, which ends
	// on Sony's Remote Play redirect page with a login code in its address. Shown in the app (a WebView),
	// the redirect is caught automatically; in Edge, the user copies the code over.

	// Sony's sign-in page.
	winrt::Windows::Foundation::Uri PsnLoginUri();

	// Whether a navigation goes to Sony's redirect page, which carries the login code.
	bool PsnIsRedirect(winrt::Windows::Foundation::Uri const &uri);

	// Exchanges the login code for the PSN account id. input is the code or the whole redirect address.
	// Returns the id as base64 (8 bytes, little endian). Throws winrt::hresult_error with a readable
	// message on failure.
	winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PsnAccountIdFromCodeAsync(winrt::hstring input);

	// Accepts an account id as base64 (12 characters) or as the decimal number, and returns it as
	// base64. Returns an empty string if the input is neither.
	std::string NormalizeAccountId(std::string const &input);
}
