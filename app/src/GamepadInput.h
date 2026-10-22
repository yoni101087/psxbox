#pragma once

namespace psx
{
	// Polls the Xbox controller and maps it to a DualShock 4 / DualSense state.
	//
	//   A/B/X/Y        -> Cross / Circle / Square / Triangle
	//   LB/RB, LT/RT   -> L1/R1, L2/R2 (analog)
	//   Sticks / clicks-> sticks, L3/R3
	//   Menu           -> Options
	//   View           -> Touchpad click (PS5 layout: Create)
	//   View + Menu    -> PS button
	//   View + LB      -> Share / Create (PS5 layout: Touchpad click)
	//   View + Menu + LB + RB -> leave the stream
	// The PS5 layout is for a PS5 controller through an adapter such as the Brook Wingman XB3, where
	// View is the Create button: pressing Create alone then sends Create.
	// View chords are delayed slightly (or sent on release when tapped), so reaching for the quit
	// combo doesn't press PS on the way.
	class GamepadInput
	{
	public:
		using StateCallback = std::function<void(ChiakiControllerState &)>;
		using QuitCallback = std::function<void()>;

		GamepadInput() = default;
		~GamepadInput();

		void Start(StateCallback state_cb, QuitCallback quit_cb, bool ps5_layout);
		void Stop();

		// Motor strengths 0-255 from the console.
		void Rumble(uint8_t left, uint8_t right);

	private:
		void Loop();
		std::optional<winrt::Windows::Gaming::Input::Gamepad> CurrentGamepad();

		StateCallback state_cb_;
		QuitCallback quit_cb_;
		bool ps5_layout_ = false;
		std::atomic<bool> stop_{ false };
		std::thread thread_;
		std::mutex pad_mutex_;
		std::optional<winrt::Windows::Gaming::Input::Gamepad> pad_;
	};
}
