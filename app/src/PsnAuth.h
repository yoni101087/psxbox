#pragma once

namespace psx
{
	// Gets the PSN account id by signing in to PSN, the same way chiaki-ng's psn-account-id.py does:
	// OAuth login in the system web authentication window, then the token info contains user_id.
	// Returns the id as base64 (8 bytes, little endian). Throws winrt::hresult_error with a readable
	// message on failure, or hresult_canceled if the user closed the window.
	winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PsnSignInAsync();

	// Accepts an account id as base64 (12 characters) or as the decimal number, and returns it as
	// base64. Returns an empty string if the input is neither.
	std::string NormalizeAccountId(std::string const &input);
}
