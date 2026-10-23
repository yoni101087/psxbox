#pragma once

namespace psx
{
	// PSN sign-in, the same way chiaki-ng's psn-account-id.py does: the user signs in to Sony in a
	// browser (Edge), which ends on Sony's Remote Play redirect page with a login code in its address.
	// That page can't hand the code to the app, so the user copies it over.

	// Sony's sign-in page, to open in the browser.
	winrt::Windows::Foundation::Uri PsnLoginUri();

	// Exchanges the login code for the PSN account id. input is the code or the whole redirect address.
	// Returns the id as base64 (8 bytes, little endian). Throws winrt::hresult_error with a readable
	// message on failure.
	winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PsnAccountIdFromCodeAsync(winrt::hstring input);

	// Accepts an account id as base64 (12 characters) or as the decimal number, and returns it as
	// base64. Returns an empty string if the input is neither.
	std::string NormalizeAccountId(std::string const &input);
}
