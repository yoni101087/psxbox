#include "pch.h"
#include "App.h"
#include "MainPage.h"

#include "src/Logger.h"

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::PsXbox::implementation
{
	App::App()
	{
		InitializeComponent();

		// Xbox: don't show the mouse-mode cursor; everything is controller driven.
		RequiresPointerMode(ApplicationRequiresPointerMode::WhenRequested);

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
		UnhandledException([](IInspectable const &, UnhandledExceptionEventArgs const &e) {
			if(IsDebuggerPresent())
			{
				auto msg = e.Message();
				__debugbreak();
			}
		});
#endif
	}

	void App::OnLaunched(LaunchActivatedEventArgs const &args)
	{
		static bool lib_initialized = false;
		if(!lib_initialized)
		{
			ChiakiErrorCode err = chiaki_lib_init();
			if(err != CHIAKI_ERR_SUCCESS)
				PSX_LOGE("chiaki_lib_init failed: %s", chiaki_error_string(err));
			HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
			if(FAILED(hr))
				PSX_LOGE("MFStartup failed: 0x%08x", hr);
			lib_initialized = true;
		}

		// Use the whole screen (the video must not be shrunk to the TV safe area; the menus add their own margins).
		ApplicationView::GetForCurrentView().SetDesiredBoundsMode(ApplicationViewBoundsMode::UseCoreWindow);

		Frame root = Window::Current().Content().try_as<Frame>();
		if(!root)
		{
			root = Frame();
			Window::Current().Content(root);
		}
		if(!args.PrelaunchActivated())
		{
			if(!root.Content())
				root.Navigate(xaml_typename<PsXbox::MainPage>(), box_value(args.Arguments()));
			Window::Current().Activate();
		}
	}
}
