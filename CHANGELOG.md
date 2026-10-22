# Changelog

## 0.3.1

- **PS5 controller through an adapter:** a new **Controller** setting for a PS5 controller connected through
  an adapter such as the Brook Wingman XB3, which turns Create into View. In this layout Create sends Create
  and Create + L1 sends the touchpad click (swapped from the Xbox layout). The Controls screen follows the
  setting.
- **Controller in the log:** the controller's name and hardware IDs are logged when it is picked up.

## 0.3.0

- **Controls screen:** a new **Controls** button on the home screen shows a picture of the Xbox controller
  with View and Menu highlighted, plus tables of every PlayStation button and face button.
- **Quick taps fixed:** a quick tap of View + Menu now opens the PS5 Control Center. Taps are sent as a
  250 ms press; the PS5 ignored the previous 80 ms press.

## 0.2.0

- **Sign in with PSN:** fills in the PSN Account ID automatically, on the Register screen and in Settings.
  No more base64.
- **Decimal Account ID:** the Account ID box also accepts the decimal number. Invalid input, such as a PSN
  username, now gets a clear message.
- **PlayStation symbols:** △ ○ ✕ □ on the home screen, the tile and the splash screen.

## 0.1.0

First version that builds and streams.

- **Leaving the stream:** holding View + Menu + LB + RB no longer presses PS on the way. View chords are
  sent after a short hold or on release, and leftover buttons stay ignored until released.
- **Controller switching:** if the controller in use disconnects, the next connected one is picked up.
- **Leaks fixed:** the connection hint timer, and the decoder list when no decoder is found.
- **Build fixes:** vcpkg baseline pinned, `VcpkgUseStatic` removed (it broke the triplet name), `nuget.config`
  added for nuget.org, missing `Windows::Foundation::Collections` namespace, and the desktop-only `ICodecAPI`
  call removed (`MF_LOW_LATENCY` already enables low-latency decoding).
