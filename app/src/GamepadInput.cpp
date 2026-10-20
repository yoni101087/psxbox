#include "pch.h"
#include "GamepadInput.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::Gaming::Input;

namespace psx
{
	static constexpr auto kPollInterval = std::chrono::milliseconds(4);
	// View is the modifier for PS / Share / touchpad, and View + Menu (PS) is on the way to the quit
	// combo. So a View chord is only sent once it has been held unchanged for kChordDelay; a shorter
	// tap is sent as a brief press when it is released.
	static constexpr auto kChordDelay = std::chrono::milliseconds(200);
	// The PS5 ignores very short PS presses, so taps are held this long.
	static constexpr auto kTapPulse = std::chrono::milliseconds(250);

	GamepadInput::~GamepadInput()
	{
		Stop();
	}

	void GamepadInput::Start(StateCallback state_cb, QuitCallback quit_cb)
	{
		Stop();
		state_cb_ = std::move(state_cb);
		quit_cb_ = std::move(quit_cb);
		stop_ = false;
		thread_ = std::thread([this] {
			init_apartment(apartment_type::multi_threaded);
			Loop();
			uninit_apartment();
		});
	}

	void GamepadInput::Stop()
	{
		stop_ = true;
		if(thread_.joinable())
			thread_.join();
		Rumble(0, 0);
	}

	std::optional<Gamepad> GamepadInput::CurrentGamepad()
	{
		auto pads = Gamepad::Gamepads();
		std::lock_guard<std::mutex> lock(pad_mutex_);
		uint32_t index = 0;
		if(pads.Size() == 0)
			pad_.reset();
		else if(!pad_ || !pads.IndexOf(*pad_, index)) // none yet, or ours was disconnected
			pad_ = pads.GetAt(0);
		return pad_;
	}

	void GamepadInput::Rumble(uint8_t left, uint8_t right)
	{
		std::optional<Gamepad> pad;
		{
			std::lock_guard<std::mutex> lock(pad_mutex_);
			pad = pad_;
		}
		if(!pad)
			return;
		GamepadVibration v = {};
		v.LeftMotor = left / 255.0;
		v.RightMotor = right / 255.0;
		try
		{
			pad->Vibration(v);
		}
		catch(...)
		{
		}
	}

	static int16_t Axis(double v)
	{
		v = std::clamp(v, -1.0, 1.0);
		return static_cast<int16_t>(v * 32767.0);
	}

	void GamepadInput::Loop()
	{
		ChiakiControllerState last;
		chiaki_controller_state_set_idle(&last);
		bool quit_sent = false;
		int refresh = 0;
		std::optional<Gamepad> pad;

		// View chord state (see kChordDelay).
		uint32_t chord = 0;           // button the held View chord maps to
		bool chord_sent = false;      // held long enough, being sent as a hold
		bool chord_canceled = false;  // quit combo formed or chord already tapped; ignore until View is released
		auto chord_since = std::chrono::steady_clock::now();
		uint32_t chord_leftover = 0;  // Menu (1) / LB (2) / RB (4) still held after View was released
		uint32_t pulse = 0;           // a tapped chord, sent briefly
		auto pulse_until = chord_since;

		while(!stop_)
		{
			if(!pad || ++refresh >= 250) // re-check connected pads about once a second
			{
				refresh = 0;
				pad = CurrentGamepad();
			}

			ChiakiControllerState s;
			chiaki_controller_state_set_idle(&s);
			if(pad)
			{
				GamepadReading r = pad->GetCurrentReading();
				auto has = [&](GamepadButtons b) { return (r.Buttons & b) == b; };

				bool view = has(GamepadButtons::View);
				bool menu = has(GamepadButtons::Menu);
				bool lb = has(GamepadButtons::LeftShoulder);
				bool rb = has(GamepadButtons::RightShoulder);

				auto now = std::chrono::steady_clock::now();
				bool quit = view && menu && lb && rb;

				if(quit)
				{
					chord_canceled = true;
					if(!quit_sent && quit_cb_)
					{
						quit_sent = true;
						quit_cb_();
					}
				}
				else
				{
					if(has(GamepadButtons::A)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
					if(has(GamepadButtons::B)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
					if(has(GamepadButtons::X)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
					if(has(GamepadButtons::Y)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
					if(has(GamepadButtons::DPadLeft)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
					if(has(GamepadButtons::DPadRight)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
					if(has(GamepadButtons::DPadUp)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
					if(has(GamepadButtons::DPadDown)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
					if(has(GamepadButtons::LeftThumbstick)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
					if(has(GamepadButtons::RightThumbstick)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

					if(view)
					{
						// While View is held, Menu / LB / RB only select the chord.
						uint32_t c = menu ? CHIAKI_CONTROLLER_BUTTON_PS : lb ? CHIAKI_CONTROLLER_BUTTON_SHARE : CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
						if(c != chord)
						{
							// Letting go of Menu / LB first ends a PS / Share tap; the rest of the View hold does nothing.
							if(c == CHIAKI_CONTROLLER_BUTTON_TOUCHPAD && chord && !chord_sent && !chord_canceled)
							{
								pulse = chord;
								pulse_until = now + kTapPulse;
								chord_canceled = true;
							}
							chord = c;
							chord_since = now;
							chord_sent = false;
						}
						if(!chord_canceled && (chord_sent || now - chord_since >= kChordDelay))
						{
							chord_sent = true;
							s.buttons |= chord;
						}
					}
					else
					{
						if(chord && !chord_sent && !chord_canceled)
						{
							pulse = chord;
							pulse_until = now + kTapPulse;
						}
						// Buttons still held from the chord stay ignored until they are let go.
						if(chord)
							chord_leftover = (menu ? 1u : 0u) | (lb ? 2u : 0u) | (rb ? 4u : 0u);
						chord_leftover &= (menu ? 1u : 0u) | (lb ? 2u : 0u) | (rb ? 4u : 0u);
						chord = 0;
						chord_sent = false;
						chord_canceled = false;

						if(menu && !(chord_leftover & 1u)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
						if(lb && !(chord_leftover & 2u)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
						if(rb && !(chord_leftover & 4u)) s.buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
					}
					if(now < pulse_until)
						s.buttons |= pulse;

					s.l2_state = static_cast<uint8_t>(std::clamp(r.LeftTrigger, 0.0, 1.0) * 255.0);
					s.r2_state = static_cast<uint8_t>(std::clamp(r.RightTrigger, 0.0, 1.0) * 255.0);
					// Windows: +Y is up. PlayStation: +Y is down.
					s.left_x = Axis(r.LeftThumbstickX);
					s.left_y = Axis(-r.LeftThumbstickY);
					s.right_x = Axis(r.RightThumbstickX);
					s.right_y = Axis(-r.RightThumbstickY);
				}

				if(!quit)
					quit_sent = false;
			}

			if(!chiaki_controller_state_equals(&s, &last))
			{
				last = s;
				if(state_cb_)
					state_cb_(s);
			}
			std::this_thread::sleep_for(kPollInterval);
		}
	}
}
