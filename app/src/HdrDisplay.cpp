#include "pch.h"
#include "HdrDisplay.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Display::Core;

namespace psx
{
	bool HdrDisplay::Supported()
	{
		// Xbox (and other HDMI-controlling devices)
		try
		{
			if(auto info = HdmiDisplayInformation::GetForCurrentView())
			{
				for(auto const &mode : info.GetSupportedDisplayModes())
				{
					if(mode.IsSmpte2084Supported())
						return true;
				}
				return false;
			}
		}
		catch(hresult_error const &)
		{
		}

		// PC: only when Windows HDR is on (Windows 10 1903 and later).
		try
		{
			auto color = DisplayInformation::GetForCurrentView().GetAdvancedColorInfo();
			return color.CurrentAdvancedColorKind() == AdvancedColorKind::HighDynamicRange;
		}
		catch(hresult_error const &)
		{
			return false;
		}
	}

	void HdrDisplay::Enable()
	{
		if(enabled_)
			return;
		HdmiDisplayInformation info{ nullptr };
		try
		{
			info = HdmiDisplayInformation::GetForCurrentView();
		}
		catch(hresult_error const &)
		{
		}
		if(!info)
			return; // PC: Windows handles HDR

		HdmiDisplayMode current = info.GetCurrentDisplayMode();
		HdmiDisplayMode hdr{ nullptr };
		for(auto const &mode : info.GetSupportedDisplayModes())
		{
			if(mode.IsSmpte2084Supported() && mode.ResolutionWidthInRawPixels() == current.ResolutionWidthInRawPixels()
				&& mode.ResolutionHeightInRawPixels() == current.ResolutionHeightInRawPixels()
				&& std::abs(mode.RefreshRate() - current.RefreshRate()) < 0.5)
			{
				hdr = mode;
				break;
			}
		}
		if(!hdr)
		{
			PSX_LOGE("The TV has no HDR mode at %ux%u %.0f Hz", current.ResolutionWidthInRawPixels(),
				current.ResolutionHeightInRawPixels(), current.RefreshRate());
			return;
		}

		enabled_ = true;
		info.RequestSetCurrentDisplayModeAsync(hdr, HdmiDisplayHdrOption::Eotf2084).Completed(
			[](IAsyncOperation<bool> const &op, AsyncStatus status) {
				bool ok = status == AsyncStatus::Completed && op.GetResults();
				if(ok)
					PSX_LOGI("TV switched to HDR");
				else
					PSX_LOGE("The TV didn't switch to HDR (check Settings > TV & display options > Video modes)");
			});
	}

	void HdrDisplay::Disable()
	{
		if(!enabled_)
			return;
		enabled_ = false;
		try
		{
			if(auto info = HdmiDisplayInformation::GetForCurrentView())
			{
				info.SetDefaultDisplayModeAsync();
				PSX_LOGI("TV back to its normal mode");
			}
		}
		catch(hresult_error const &e)
		{
			PSX_LOGE("Restoring the TV mode failed: %s", Narrow(e.message()).c_str());
		}
	}
}
