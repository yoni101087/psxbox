#pragma once

#include "MainPage.g.h"

#include "src/Discovery.h"
#include "src/HdrDisplay.h"
#include "src/MicCapture.h"
#include "src/Registration.h"
#include "src/StreamSession.h"
#include "src/VideoRenderer.h"

namespace winrt::PsXbox::implementation
{
	struct MainPage : MainPageT<MainPage>
	{
		MainPage();
		~MainPage();

		// XAML event handlers
		void OnRefreshClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnAddIpClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnAddIpConfirmClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnSettingsClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnControlsClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnSaveSettingsClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnForgetConsolesClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnBackToHomeClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnRegisterClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		fire_and_forget OnPsnSignInClick(IInspectable const &sender, Windows::UI::Xaml::RoutedEventArgs const &args);
		void OnConsoleClicked(IInspectable const &sender, Windows::UI::Xaml::Controls::ItemClickEventArgs const &args);
		void OnVideoPanelSizeChanged(IInspectable const &sender, Windows::UI::Xaml::SizeChangedEventArgs const &args);
		void OnVideoPanelScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel const &sender, IInspectable const &args);

	private:
		enum class Screen { Home, Register, Settings, AddIp, Controls, Stream };

		struct ConsoleEntry
		{
			std::string mac;
			std::string name;
			std::string addr;
			bool registered = false;
			bool discovered = false;
			bool ps5 = false;
			ChiakiTarget target = CHIAKI_TARGET_PS5_UNKNOWN;
			ChiakiDiscoveryHostState state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
			std::string running_app;
		};

		void Show(Screen screen);
		void RunOnUi(std::function<void()> fn);
		void RefreshConsoleList();
		void RefreshLog();
		void SetStatus(std::string const &text);
		void StartDiscovery();
		void StopDiscovery();
		void OpenRegister(ConsoleEntry const &entry);
		void ConnectTo(ConsoleEntry const &entry);
		void StartStream(psx::RegisteredHost const &host, std::string const &addr);
		void EndStream(std::string const &reason);
		fire_and_forget StartMicrophone();
		void ShowStreamMessage(hstring const &text);
		void LoadSettingsUi();

		Screen screen_ = Screen::Home;
		std::unique_ptr<psx::Discovery> discovery_;
		std::vector<psx::DiscoveredHost> discovered_;
		std::vector<ConsoleEntry> entries_;
		Windows::Foundation::Collections::IObservableVector<IInspectable> console_items_{ nullptr };

		psx::Registration registration_;
		ConsoleEntry register_entry_;

		psx::VideoRenderer renderer_;
		psx::HdrDisplay hdr_display_;
		psx::MicCapture mic_;
		bool panel_attached_ = false;
		psx::StreamSession session_;
		std::string pending_wake_mac_;
		Windows::System::Display::DisplayRequest display_request_{ nullptr };
		bool display_active_ = false;

		std::atomic<bool> log_dirty_{ false };
		std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

		event_token back_token_{};
		event_token suspend_token_{};
		event_token resume_token_{};
	};
}

namespace winrt::PsXbox::factory_implementation
{
	struct MainPage : MainPageT<MainPage, implementation::MainPage>
	{
	};
}
