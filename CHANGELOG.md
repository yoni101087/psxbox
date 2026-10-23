# Changelog

## 0.4.4

- **Controller rumble:** the controller is no longer also opened as a raw device (added in 0.3.1 to log its
  name), which may have stopped rumble. The first rumble of each stream and any rumble error are now logged.
- **HDR:** a new video codec setting, **H.265 HDR (PS5 only, HDR TV)**. The PS5 streams 10-bit HDR video,
  the app decodes it to P010 and shows it as HDR10, and on Xbox it switches the TV to HDR while HDR frames are
  shown (back to normal when the stream ends). SDR frames over the same codec are still shown in SDR. If the
  TV can't show HDR, the app streams plain H.265 instead.
- **Microphone:** a new **Microphone** setting sends a headset plugged into the controller to the console
  (for voice chat in games and parties). It starts muted; **View + Y** turns it on and off, with a short
  message on screen. The Xbox asks once for permission to use the microphone.
- **Sign in with PSN fixed:** Sony's sign-in page no longer runs in the system's older web view (it stays on
  its loading spinner). It is now shown inside the app with WebView2, the Chromium engine Edge uses, and the
  app picks up the result by itself once you're signed in. The left stick moves a cursor in the page and A
  selects; type with a USB keyboard or the on-screen one.
- **Sign in on your phone:** the sign-in screen also shows a QR code. It opens a page served by the Xbox on
  your network; sign in to PSN on the phone, paste the address you land on, and the Xbox finishes by itself.
  (Replaces the Edge fallback, which needed the code copied on the Xbox.)
- **New dependencies:** WinUI 2 (`Microsoft.UI.Xaml.2.8.appx`) and `Microsoft.VCLibs.x64.14.00.Desktop.appx`,
  needed by WebView2. Install them with the app.

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
