#include "pch.h"
#include "MainPage.h"
#if __has_include("MainPage.g.cpp")
#include "MainPage.g.cpp"
#endif

#include "src/Logger.h"
#include "src/PsnAuth.h"
#include "src/Settings.h"

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;

namespace winrt::PsXbox::implementation
{
	namespace
	{
		const char *StateString(ChiakiDiscoveryHostState s)
		{
			switch(s)
			{
				case CHIAKI_DISCOVERY_HOST_STATE_READY: return "Ready";
				case CHIAKI_DISCOVERY_HOST_STATE_STANDBY: return "Rest mode";
				default: return "Not found";
			}
		}

		std::string Trim(std::string s)
		{
			while(!s.empty() && isspace(static_cast<unsigned char>(s.back())))
				s.pop_back();
			size_t i = 0;
			while(i < s.size() && isspace(static_cast<unsigned char>(s[i])))
				i++;
			return s.substr(i);
		}

		int SelectedTag(ComboBox const &box, int def)
		{
			auto item = box.SelectedItem().try_as<ComboBoxItem>();
			if(!item)
				return def;
			return std::stoi(psx::Narrow(unbox_value<hstring>(item.Tag())));
		}

		void SelectByTag(ComboBox const &box, int tag)
		{
			for(uint32_t i = 0; i < box.Items().Size(); i++)
			{
				auto item = box.Items().GetAt(i).as<ComboBoxItem>();
				if(std::stoi(psx::Narrow(unbox_value<hstring>(item.Tag()))) == tag)
				{
					box.SelectedIndex(static_cast<int32_t>(i));
					return;
				}
			}
			box.SelectedIndex(0);
		}
	}

	MainPage::MainPage()
	{
		InitializeComponent();

		console_items_ = single_threaded_observable_vector<IInspectable>();
		ConsoleList().ItemsSource(console_items_);

		display_request_ = Windows::System::Display::DisplayRequest();

		psx::Logger::Get().SetOnChanged([this, alive = std::weak_ptr<bool>(alive_)] {
			if(alive.expired() || log_dirty_.exchange(true))
				return;
			RunOnUi([this] { RefreshLog(); });
		});

		back_token_ = SystemNavigationManager::GetForCurrentView().BackRequested([this](auto const &, BackRequestedEventArgs const &args) {
			if(screen_ == Screen::Stream)
			{
				// B is a game button while streaming.
				args.Handled(true);
			}
			else if(screen_ != Screen::Home)
			{
				args.Handled(true);
				registration_.Stop();
				Show(Screen::Home);
			}
		});

		// While streaming, keep the controller from driving XAML focus.
		PreviewKeyDown([this](auto const &, KeyRoutedEventArgs const &args) {
			if(screen_ == Screen::Stream)
				args.Handled(true);
		});

		suspend_token_ = Application::Current().Suspending([this](auto const &, SuspendingEventArgs const &) {
			if(session_.Active())
				EndStream("The app was suspended.");
			StopDiscovery();
		});
		resume_token_ = Application::Current().Resuming([this](auto const &, auto const &) {
			StartDiscovery();
		});

		// The TV follows the stream: HDR10 mode while HDR frames are shown.
		renderer_.SetOnHdrChanged([this, alive = std::weak_ptr<bool>(alive_)](bool hdr) {
			if(alive.expired())
				return;
			RunOnUi([this, hdr] {
				if(hdr && screen_ == Screen::Stream)
					hdr_display_.Enable();
				else
					hdr_display_.Disable();
			});
		});

		if(!renderer_.InitDevice())
			SetStatus("Graphics initialisation failed, streaming will not work. See the log below.");

		PSX_LOGI("PS Xbox started. %zu registered console(s).", psx::Settings::Get().Hosts().size());
		RefreshConsoleList();
		StartDiscovery();
		Show(Screen::Home);
	}

	MainPage::~MainPage()
	{
		*alive_ = false;
		alive_.reset();
		psx::Logger::Get().SetOnChanged(nullptr);
		SystemNavigationManager::GetForCurrentView().BackRequested(back_token_);
		Application::Current().Suspending(suspend_token_);
		Application::Current().Resuming(resume_token_);
		session_.Stop();
		registration_.Stop();
		StopDiscovery();
	}

	void MainPage::RunOnUi(std::function<void()> fn)
	{
		Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [weak = get_weak(), fn = std::move(fn)] {
			if(auto self = weak.get())
				fn();
		});
	}

	void MainPage::Show(Screen screen)
	{
		screen_ = screen;
		HomePanel().Visibility(screen == Screen::Home ? Visibility::Visible : Visibility::Collapsed);
		RegisterPanel().Visibility(screen == Screen::Register ? Visibility::Visible : Visibility::Collapsed);
		SettingsPanel().Visibility(screen == Screen::Settings ? Visibility::Visible : Visibility::Collapsed);
		AddIpPanel().Visibility(screen == Screen::AddIp ? Visibility::Visible : Visibility::Collapsed);
		ControlsPanel().Visibility(screen == Screen::Controls ? Visibility::Visible : Visibility::Collapsed);
		StreamPanel().Visibility(screen == Screen::Stream ? Visibility::Visible : Visibility::Collapsed);
		if(screen == Screen::Controls)
			ControlsPulse().Begin();
		else
			ControlsPulse().Stop();

		switch(screen)
		{
			case Screen::Home:
				if(console_items_.Size() > 0)
					ConsoleList().Focus(FocusState::Programmatic);
				else
					RefreshButton().Focus(FocusState::Programmatic);
				break;
			case Screen::Register:
				(AccountIdBox().Text().empty() ? AccountIdBox() : PinBox()).Focus(FocusState::Programmatic);
				break;
			case Screen::Settings:
				ResolutionBox().Focus(FocusState::Programmatic);
				break;
			case Screen::AddIp:
				IpBox().Focus(FocusState::Programmatic);
				break;
			case Screen::Controls:
				ControlsBackButton().Focus(FocusState::Programmatic);
				break;
			case Screen::Stream:
				break;
		}
	}

	void MainPage::SetStatus(std::string const &text)
	{
		StatusText().Text(psx::Widen(text));
	}

	void MainPage::RefreshLog()
	{
		log_dirty_ = false;
		std::string text;
		auto lines = psx::Logger::Get().Lines();
		size_t start = lines.size() > 60 ? lines.size() - 60 : 0;
		for(size_t i = start; i < lines.size(); i++)
			text += lines[i] + "\n";
		LogText().Text(psx::Widen(text));
		LogScroll().ChangeView(nullptr, LogScroll().ScrollableHeight() + 1000.0, nullptr, true);
	}

	// ---------------------------------------------------------------- discovery / console list

	void MainPage::StartDiscovery()
	{
		if(discovery_ && discovery_->Running())
			return;
		discovery_ = std::make_unique<psx::Discovery>([this, alive = std::weak_ptr<bool>(alive_)](std::vector<psx::DiscoveredHost> const &hosts) {
			if(alive.expired())
				return;
			RunOnUi([this, hosts] {
				discovered_ = hosts;
				RefreshConsoleList();

				// Auto-connect once a console we woke up reports Ready.
				if(!pending_wake_mac_.empty() && screen_ == Screen::Home)
				{
					for(auto const &e : entries_)
					{
						if(e.mac == pending_wake_mac_ && e.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
						{
							pending_wake_mac_.clear();
							ConnectTo(e);
							break;
						}
					}
				}
			});
		});
		if(!discovery_->Start())
			SetStatus("Could not start console discovery. Check the network connection.");
	}

	void MainPage::StopDiscovery()
	{
		if(discovery_)
			discovery_->Stop();
	}

	void MainPage::RefreshConsoleList()
	{
		std::vector<ConsoleEntry> entries;
		for(auto const &h : psx::Settings::Get().Hosts())
		{
			ConsoleEntry e;
			e.mac = h.mac;
			e.name = h.nickname;
			e.addr = h.last_addr;
			e.registered = true;
			e.ps5 = h.IsPs5();
			e.target = h.target;
			entries.push_back(e);
		}
		for(auto const &d : discovered_)
		{
			auto it = std::find_if(entries.begin(), entries.end(), [&](auto const &e) { return !d.mac.empty() && e.mac == d.mac; });
			if(it == entries.end())
			{
				ConsoleEntry e;
				e.mac = d.mac;
				e.ps5 = d.ps5;
				e.target = d.ps5 ? CHIAKI_TARGET_PS5_1 : d.target;
				entries.push_back(e);
				it = entries.end() - 1;
			}
			it->discovered = true;
			it->name = d.name.empty() ? it->name : d.name;
			it->addr = d.addr;
			it->state = d.state;
			it->running_app = d.running_app;
		}

		// Only rebuild the list when something visible changed, so controller focus isn't lost.
		std::vector<hstring> texts;
		for(auto const &e : entries)
		{
			std::string t = (e.name.empty() ? std::string("PlayStation") : e.name) + "   |   " + (e.ps5 ? "PS5" : "PS4") + "   |   " + StateString(e.state)
				+ "   |   " + (e.registered ? "Registered" : "Not registered");
			if(!e.addr.empty())
				t += "   |   " + e.addr;
			if(!e.running_app.empty())
				t += "\n    Playing: " + e.running_app;
			texts.push_back(psx::Widen(t));
		}

		bool same = texts.size() == console_items_.Size();
		for(size_t i = 0; same && i < texts.size(); i++)
			same = unbox_value<hstring>(console_items_.GetAt(static_cast<uint32_t>(i))) == texts[i];
		entries_ = std::move(entries);
		if(same)
			return;

		int32_t focused = -1;
		if(auto el = FocusManager::GetFocusedElement().try_as<ListViewItem>())
			focused = ConsoleList().IndexFromContainer(el);

		console_items_.Clear();
		for(auto const &t : texts)
			console_items_.Append(box_value(t));

		if(screen_ == Screen::Home && focused >= 0 && focused < static_cast<int32_t>(console_items_.Size()))
		{
			ConsoleList().UpdateLayout();
			if(auto item = ConsoleList().ContainerFromIndex(focused).try_as<ListViewItem>())
				item.Focus(FocusState::Programmatic);
		}
	}

	void MainPage::OnConsoleClicked(IInspectable const &, ItemClickEventArgs const &args)
	{
		uint32_t index = 0;
		if(!console_items_.IndexOf(args.ClickedItem(), index) || index >= entries_.size())
			return;
		ConsoleEntry entry = entries_[index];

		if(!entry.registered)
		{
			if(!entry.discovered)
			{
				SetStatus("Console not found on the network.");
				return;
			}
			OpenRegister(entry);
			return;
		}
		ConnectTo(entry);
	}

	void MainPage::OnRefreshClick(IInspectable const &, RoutedEventArgs const &)
	{
		StopDiscovery();
		discovered_.clear();
		RefreshConsoleList();
		StartDiscovery();
		SetStatus("Searching for consoles...");
	}

	// ---------------------------------------------------------------- add by IP

	void MainPage::OnAddIpClick(IInspectable const &, RoutedEventArgs const &)
	{
		IpBox().Text(L"");
		Show(Screen::AddIp);
	}

	void MainPage::OnAddIpConfirmClick(IInspectable const &, RoutedEventArgs const &)
	{
		std::string ip = Trim(psx::Narrow(IpBox().Text()));
		in_addr a{};
		if(inet_pton(AF_INET, ip.c_str(), &a) != 1)
		{
			SetStatus("That is not a valid IPv4 address.");
			Show(Screen::Home);
			return;
		}
		psx::Settings::Get().AddManualAddress(ip);
		PSX_LOGI("Added manual address %s", ip.c_str());
		Show(Screen::Home);
		OnRefreshClick(nullptr, nullptr);
	}

	// ---------------------------------------------------------------- settings

	void MainPage::LoadSettingsUi()
	{
		auto s = psx::Settings::Get().Stream();
		SelectByTag(ResolutionBox(), s.resolution);
		SelectByTag(FpsBox(), s.fps);
		SelectByTag(CodecBox(), static_cast<int>(s.codec));
		BitrateBox().Text(s.bitrate_kbps ? to_hstring(s.bitrate_kbps) : hstring());
		SelectByTag(ControllerBox(), s.ps5_layout ? 1 : 0);
		SettingsAccountIdBox().Text(psx::Widen(psx::Settings::Get().PsnAccountId()));
		SettingsSignInStatus().Text(L"");

		std::string list;
		for(auto const &h : psx::Settings::Get().Hosts())
			list += h.nickname + " (" + (h.IsPs5() ? "PS5" : "PS4") + ", " + h.mac + ")\n";
		RegisteredList().Text(psx::Widen(list.empty() ? "None" : list));
	}

	void MainPage::OnSettingsClick(IInspectable const &, RoutedEventArgs const &)
	{
		LoadSettingsUi();
		Show(Screen::Settings);
	}

	void MainPage::OnControlsClick(IInspectable const &, RoutedEventArgs const &)
	{
		bool ps5 = psx::Settings::Get().Stream().ps5_layout;
		ViewDoes().Text(ps5 ? L"Create (Share)" : L"Touchpad click");
		ViewLbDoes().Text(ps5 ? L"Touchpad click" : L"Create (Share)");
		Ps5LayoutNote().Visibility(ps5 ? Visibility::Visible : Visibility::Collapsed);
		Show(Screen::Controls);
	}

	void MainPage::OnSaveSettingsClick(IInspectable const &, RoutedEventArgs const &)
	{
		psx::StreamSettings s;
		s.resolution = static_cast<ChiakiVideoResolutionPreset>(SelectedTag(ResolutionBox(), CHIAKI_VIDEO_RESOLUTION_PRESET_1080p));
		s.fps = static_cast<ChiakiVideoFPSPreset>(SelectedTag(FpsBox(), CHIAKI_VIDEO_FPS_PRESET_60));
		s.codec = static_cast<ChiakiCodec>(SelectedTag(CodecBox(), CHIAKI_CODEC_H264));
		std::string bitrate = Trim(psx::Narrow(BitrateBox().Text()));
		s.bitrate_kbps = bitrate.empty() ? 0 : static_cast<unsigned int>(strtoul(bitrate.c_str(), nullptr, 10));
		s.ps5_layout = SelectedTag(ControllerBox(), 0) == 1;
		std::string account_input = Trim(psx::Narrow(SettingsAccountIdBox().Text()));
		std::string account_id = psx::NormalizeAccountId(account_input);
		if(!account_input.empty() && account_id.empty())
		{
			SettingsSignInStatus().Text(L"That PSN Account ID isn't valid. Select Sign in with PSN to fill it in.");
			return;
		}
		psx::Settings::Get().SetStream(s);
		psx::Settings::Get().SetPsnAccountId(account_id);
		SetStatus("Settings saved.");
		Show(Screen::Home);
	}

	void MainPage::OnForgetConsolesClick(IInspectable const &, RoutedEventArgs const &)
	{
		auto hosts = psx::Settings::Get().Hosts();
		for(auto const &h : hosts)
			psx::Settings::Get().RemoveHost(h.mac);
		LoadSettingsUi();
		RefreshConsoleList();
	}

	void MainPage::OnBackToHomeClick(IInspectable const &, RoutedEventArgs const &)
	{
		registration_.Stop();
		Show(Screen::Home);
	}

	// ---------------------------------------------------------------- registration

	void MainPage::OpenRegister(ConsoleEntry const &entry)
	{
		register_entry_ = entry;
		RegisterTargetText().Text(psx::Widen((entry.name.empty() ? std::string("PlayStation") : entry.name) + " (" + (entry.ps5 ? "PS5" : "PS4") + ") at " + entry.addr));
		AccountIdBox().Text(psx::Widen(psx::Settings::Get().PsnAccountId()));
		PinBox().Text(L"");
		RegisterStatus().Text(L"");
		RegisterButton().IsEnabled(true);
		Show(Screen::Register);
	}

	void MainPage::OnRegisterClick(IInspectable const &, RoutedEventArgs const &)
	{
		std::string account_id = psx::NormalizeAccountId(Trim(psx::Narrow(AccountIdBox().Text())));
		if(account_id.empty())
		{
			RegisterStatus().Text(L"Select Sign in with PSN first, so the app knows your PSN Account ID.");
			return;
		}
		std::string pin_str = Trim(psx::Narrow(PinBox().Text()));
		if(pin_str.size() != 8 || !std::all_of(pin_str.begin(), pin_str.end(), [](char c) { return c >= '0' && c <= '9'; }))
		{
			RegisterStatus().Text(L"The PIN must be 8 digits.");
			return;
		}
		uint32_t pin = static_cast<uint32_t>(strtoul(pin_str.c_str(), nullptr, 10));
		psx::Settings::Get().SetPsnAccountId(account_id);

		RegisterButton().IsEnabled(false);
		RegisterStatus().Text(L"Registering...");
		bool ok = registration_.Start(register_entry_.addr, register_entry_.target, account_id, pin,
			[this, alive = std::weak_ptr<bool>(alive_)](std::optional<psx::RegisteredHost> const &host) {
				if(alive.expired())
					return;
				RunOnUi([this, host] {
					registration_.Stop();
					RegisterButton().IsEnabled(true);
					if(!host)
					{
						RegisterStatus().Text(L"Registration failed. Check the PIN (it expires quickly) and the Account ID; details are in the log on the home screen.");
						return;
					}
					psx::Settings::Get().AddOrUpdateHost(*host);
					RefreshConsoleList();
					SetStatus("Registered " + host->nickname + ". Select it to start playing.");
					Show(Screen::Home);
				});
			});
		if(!ok)
		{
			RegisterButton().IsEnabled(true);
			RegisterStatus().Text(L"Could not start registration. Details are in the log on the home screen.");
		}
	}

	fire_and_forget MainPage::OnPsnSignInClick(IInspectable const &, RoutedEventArgs const &)
	{
		auto weak = get_weak();
		bool on_register = screen_ == Screen::Register;
		auto status = [this, on_register](hstring const &text) {
			(on_register ? RegisterStatus() : SettingsSignInStatus()).Text(text);
		};
		RegisterSignInButton().IsEnabled(false);
		SettingsSignInButton().IsEnabled(false);
		status(L"Signing in to PSN...");

		hstring id;
		hstring error;
		try
		{
			id = co_await psx::PsnSignInAsync();
		}
		catch(hresult_canceled const &)
		{
			error = L"Sign-in canceled.";
		}
		catch(hresult_error const &e)
		{
			error = L"PSN sign-in failed: " + e.message();
		}

		auto self = weak.get();
		if(!self)
			co_return;
		RegisterSignInButton().IsEnabled(true);
		SettingsSignInButton().IsEnabled(true);
		if(!error.empty())
		{
			status(error);
			co_return;
		}
		psx::Settings::Get().SetPsnAccountId(psx::Narrow(id));
		AccountIdBox().Text(id);
		SettingsAccountIdBox().Text(id);
		status(L"Signed in. Your PSN Account ID is filled in.");
	}

	// ---------------------------------------------------------------- streaming

	void MainPage::ConnectTo(ConsoleEntry const &entry)
	{
		auto host = psx::Settings::Get().FindHost(entry.mac);
		if(!host)
			return;
		std::string addr = entry.addr.empty() ? host->last_addr : entry.addr;
		if(addr.empty())
		{
			SetStatus("No address known for this console. Turn it on or add it by IP.");
			return;
		}

		if(entry.discovered && entry.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
		{
			if(psx::Discovery::Wakeup(addr, host->regist_key, host->IsPs5()))
			{
				pending_wake_mac_ = entry.mac;
				SetStatus("Waking up " + entry.name + "... streaming will start when it is ready.");
			}
			else
				SetStatus("Failed to send the wake-up signal.");
			return;
		}

		if(entry.addr != host->last_addr && !entry.addr.empty())
		{
			psx::RegisteredHost updated = *host;
			updated.last_addr = entry.addr;
			psx::Settings::Get().AddOrUpdateHost(updated);
			host = psx::Settings::Get().FindHost(entry.mac);
		}
		StartStream(*host, addr);
	}

	void MainPage::StartStream(psx::RegisteredHost const &host, std::string const &addr)
	{
		StopDiscovery();
		Show(Screen::Stream);
		StreamStatus().Text(psx::Widen("Connecting to " + host.nickname + "...\nHold View + Menu + LB + RB to leave."));
		StreamOverlay().Visibility(Visibility::Visible);

		if(!panel_attached_)
		{
			try
			{
				renderer_.AttachPanel(VideoPanel());
				panel_attached_ = true;
			}
			catch(hresult_error const &e)
			{
				PSX_LOGE("Failed to attach video output: %s", psx::Narrow(e.message()).c_str());
				EndStream("Video output could not be created.");
				return;
			}
		}

		psx::StreamSession::Callbacks cb;
		cb.connected = [this, alive = std::weak_ptr<bool>(alive_)] {
			if(alive.expired())
				return;
			RunOnUi([this] {
				StreamStatus().Text(L"Connected. Hold View + Menu + LB + RB to leave.");
				// Hide the hint after a few seconds.
				auto timer = DispatcherTimer();
				timer.Interval(std::chrono::seconds(5));
				// Get the timer from the sender: capturing it would keep it alive forever.
				timer.Tick([this](IInspectable const &sender, IInspectable const &) {
					sender.as<DispatcherTimer>().Stop();
					if(screen_ == Screen::Stream)
						StreamOverlay().Visibility(Visibility::Collapsed);
				});
				timer.Start();
			});
		};
		cb.ended = [this, alive = std::weak_ptr<bool>(alive_)](std::string const &reason) {
			if(alive.expired())
				return;
			RunOnUi([this, reason] { EndStream(reason); });
		};

		psx::StreamSettings settings = psx::Settings::Get().Stream();
		if(chiaki_codec_is_hdr(settings.codec) && !psx::HdrDisplay::Supported())
		{
			PSX_LOGI("This display can't show HDR, streaming H.265 without HDR");
			settings.codec = CHIAKI_CODEC_H265;
		}
		if(!session_.Start(host, addr, settings, &renderer_, std::move(cb)))
		{
			EndStream("Could not start the session. See the log for details.");
			return;
		}
		if(!display_active_)
		{
			display_request_.RequestActive();
			display_active_ = true;
		}
	}

	void MainPage::EndStream(std::string const &reason)
	{
		session_.Stop();
		hdr_display_.Disable();
		if(display_active_)
		{
			display_request_.RequestRelease();
			display_active_ = false;
		}
		SetStatus(reason.empty() ? "Stream ended." : "Stream ended: " + reason);
		Show(Screen::Home);
		StartDiscovery();
	}

	void MainPage::OnVideoPanelSizeChanged(IInspectable const &, SizeChangedEventArgs const &args)
	{
		if(!panel_attached_)
			return;
		renderer_.Resize(args.NewSize().Width, args.NewSize().Height, VideoPanel().CompositionScaleX(), VideoPanel().CompositionScaleY());
	}

	void MainPage::OnVideoPanelScaleChanged(SwapChainPanel const &sender, IInspectable const &)
	{
		if(!panel_attached_)
			return;
		renderer_.Resize(static_cast<float>(sender.ActualWidth()), static_cast<float>(sender.ActualHeight()), sender.CompositionScaleX(), sender.CompositionScaleY());
	}
}
