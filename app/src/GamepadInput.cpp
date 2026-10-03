#include "pch.h"
#include "GamepadInput.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::Gaming::Input;

namespace psx
{
	static constexpr auto kPollInterval = std::chrono::milliseconds(4);

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
		if(pads.Size() == 0)
			pad_.reset();
		else if(!pad_)
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

				if(view && menu && lb && rb)
				{
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
					if(rb) s.buttons |= CHIAKI_CONTROLLER_BUTTON_R1;

					if(view && menu)
						s.buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
					else if(view && lb)
						s.buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE;
					else
					{
						if(view) s.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
						if(menu) s.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
						if(lb) s.buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
					}

					s.l2_state = static_cast<uint8_t>(std::clamp(r.LeftTrigger, 0.0, 1.0) * 255.0);
					s.r2_state = static_cast<uint8_t>(std::clamp(r.RightTrigger, 0.0, 1.0) * 255.0);
					// Windows: +Y is up. PlayStation: +Y is down.
					s.left_x = Axis(r.LeftThumbstickX);
					s.left_y = Axis(-r.LeftThumbstickY);
					s.right_x = Axis(r.RightThumbstickX);
					s.right_y = Axis(-r.RightThumbstickY);
				}

				if(!(view && menu && lb && rb))
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
