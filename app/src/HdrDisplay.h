#pragma once

namespace psx
{
	// Switches the TV to HDR10 while an HDR stream is shown. On Xbox an app has to ask for the HDMI mode
	// itself; on a PC, Windows' own HDR setting decides, so there is nothing to switch.
	class HdrDisplay
	{
	public:
		// UI thread. Whether the display can show HDR10 (Xbox: a mode with SMPTE 2084; PC: HDR is on).
		static bool Supported();

		// UI thread. Enable keeps the current resolution and refresh rate.
		void Enable();
		void Disable();

	private:
		bool enabled_ = false;
	};
}
