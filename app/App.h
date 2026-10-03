#pragma once

#include "App.xaml.g.h"

namespace winrt::PsXbox::implementation
{
	struct App : AppT<App>
	{
		App();

		void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const &args);
	};
}
