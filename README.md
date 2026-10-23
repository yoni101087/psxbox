# PS Xbox

A PlayStation Remote Play client for Xbox (UWP, Dev Mode), in the spirit of PXPlay.
Stream your PS4 or PS5 to an Xbox One / Series X|S on your local network and play it with the Xbox controller.

It's built on the protocol library from [chiaki-ng](https://github.com/streetpea/chiaki-ng),
the open-source, reverse-engineered implementation of Sony's Remote Play protocol.

> **Status:** works. Tested streaming a PS5 to an Xbox in Dev Mode over the local network.
> See [CHANGELOG.md](CHANGELOG.md) for what changed in each version.

- [Install a release on your Xbox](#install-a-release-on-your-xbox) (no build needed)
- [First connection](#first-connection)
- [Controls](#controls)
- [Build it yourself](#build-it-yourself)
- [Troubleshooting](#troubleshooting)

## Install a release on your Xbox

You need an Xbox in **Dev Mode**. That requires a Microsoft Partner Center developer account (a one-time fee).

1. **Put the Xbox in Dev Mode.** Install the **Xbox Dev Mode** app from the Store on the Xbox, activate it
   with the code it shows in Partner Center, then switch to Dev Mode.
2. **Turn on Device Portal.** In **Dev Home** on the Xbox, note the IP address, open **Remote Access settings**
   and enable **Device Portal**.
3. **Download** these two files from the latest [release](../../releases/latest):
   - `PsXbox_<version>_x64.msix`: the app
   - `Microsoft.VCLibs.x64.14.00.appx`: the C++ runtime it needs
4. **Install.** On a PC on the same network, open `https://<xbox-ip>:11443` (accept the certificate
   warning; the Xbox uses a self-signed one). Go to **Home → My games & apps → Add**, select the `.msix`,
   select **Next**, add the VCLibs `.appx` as a dependency, then select **Start**.
5. **Set the app type to Game.** In Dev Home, highlight **PS Xbox** → **View details** → change **App type**
   from *App* to **Game**. Apps only get a fraction of the GPU and memory; games get the full console.

**Updating:** install the new `.msix` the same way. Don't uninstall the old version first: an update keeps
your registered consoles and PSN Account ID, while uninstalling deletes them.

## First connection

1. **Turn on Remote Play on the PlayStation:**
   - PS5: Settings → System → Remote Play → enable.
   - PS4: Settings → Remote Play Connection Settings → enable. Also set it as your primary PS4 if you
     want wake from rest mode.
2. **Open PS Xbox.** Your console should show up in the list. If not, use **Add by IP**.
3. **Select the console**, then select **Sign in with PSN** and sign in with the PSN account you use on the
   console (in Edge; see [About the PSN Account ID](#about-the-psn-account-id)). This fills in your
   **PSN Account ID**, which registration needs. It's stored on the Xbox, so you
   only do this once.
4. **On the PlayStation**, open **Link Device** (PS5: Settings → System → Remote Play) or **Add Device**
   (PS4: Settings → Remote Play Connection Settings). Enter the 8-digit PIN it shows in the app and select
   **Register**. The PIN expires after a few minutes.
5. **Select the registered console** to start streaming. If it's in rest mode, the app wakes it and
   connects once it's ready.

### About the PSN Account ID

The Account ID is not your PSN username (online ID). It's a number PSN assigns to your account, which only
Sony's servers can look up, and only after you sign in. **Sign in with PSN** does that for you:

1. It opens Sony's sign-in page in Edge. Sign in there.
2. Edge then lands on a page from `remoteplay.dl.playstation.net` that may look empty or show an error. That's
   expected: the login code is in the address bar, after `code=` (up to any `&`).
3. Go back to PS Xbox (Xbox button → PS Xbox) and type the code, or paste the whole address, into the box it
   shows. The code expires after a few minutes.

If that doesn't work, you can type the ID in yourself, in either form:

- **Base64**, 12 characters like `AbCdEfGhIjk=`. To get it on a PC, run
  `python third_party\chiaki-ng\scripts\psn-account-id.py` (needs `pip install requests`), open the link it
  prints, sign in, and paste back the URL you land on.
- **The decimal number**, like `1234567890123456789`.

## Controls

The app has a **Controls** screen (on the home screen) with a picture of the controller.

**HDR:** in **Settings → Video codec**, choose **H.265 HDR (PS5 only, HDR TV)**. HDR needs:
- On the Xbox: **Settings → General → TV & display options → Video modes → Allow HDR10** turned on (an HDR TV).
- On the PS5: **Settings → Screen and Video → Video Output → HDR** on, and a game that uses HDR.

The app switches the TV to HDR while HDR frames are shown and back when the stream ends. If the TV can't show
HDR, it streams plain H.265 instead.

**Microphone:** turn on **Settings → Microphone** and plug a headset into the controller. The microphone
starts muted; press **View + Y** to turn it on or off (a message shows which). The first time, the Xbox asks
whether the app may use the microphone. If it says the microphone is unavailable, check **Settings → Privacy
& online safety → App privacy → Microphone** on the Xbox.

**PS5 controller through an adapter** (such as the Brook Wingman XB3): choose it under **Settings →
Controller**. The adapter makes it an Xbox controller, so Create is View, Options is Menu and L1 / R1 are
LB / RB in the table below, with one difference: Create alone sends **Create**, and Create + L1 sends the
**touchpad click**. The controller's PS button opens the Xbox guide; use Create + Options for PS. The
touchpad surface, motion sensors and adaptive triggers don't pass through the adapter.

| Xbox | PlayStation |
|---|---|
| A / B / X / Y | ✕ Cross / ○ Circle / □ Square / △ Triangle |
| LB / RB, LT / RT | L1 / R1, L2 / R2 (analog) |
| Sticks, stick clicks | Sticks, L3 / R3 |
| D-pad | D-pad |
| Menu | Options |
| View | Touchpad click |
| **Tap View + Menu** | PS button: Control Center (power, downloads) |
| **Hold View + Menu** (about 1 s) | PS button held: PS5 home screen |
| View + LB | Create / Share |
| **View + Menu + LB + RB** | **Leave the stream** |

- View and Menu are the two small buttons in the middle of the controller. The big Xbox button can't be
  used: the Xbox keeps it for its own guide.
- A View chord (PS, Create, touchpad) is sent once it has been held for 0.2 s, or as a short press when you
  let go of a quick tap. That way, reaching for the leave combo doesn't press PS on the way.
- Rumble is forwarded to the Xbox controller.
- **Powering off the PlayStation:** tap View + Menu, go to the power icon at the right of the Control Center,
  and choose **Enter Rest Mode** (the app can wake it later) or **Turn Off**.

## How it works

```
PS4/PS5 ──LAN──► chiaki-ng lib (C, clang-cl)          ── discovery, PIN registration, session,
                   │                                     Takion/UDP, crypto, FEC, Opus
                   ├─ video (H.264/HEVC Annex-B) ──► Media Foundation decoder (hardware, D3D11)
                   │                                   └─► D3D11 video processor ─► SwapChainPanel
                   ├─ audio (Opus → PCM) ───────────► XAudio2
                   └─ controller state ◄──────────── Windows.Gaming.Input (Xbox pad)
UI: C++/WinRT XAML, controller navigable
```

| Path | Contents |
|---|---|
| `third_party/chiaki-ng` | git submodule (upstream, unmodified) |
| `chiaki-uwp/` | static lib project for chiaki's `lib/` + FEC + nanopb, built for UWP |
| `chiaki-uwp/stubs/remote_stubs.c` | stubs for PSN "Remote Play over Internet" (LAN-only build, no curl/json-c/libevent) |
| `chiaki-uwp/generated/` | `takion.pb.c/h` pre-generated with nanopb |
| `app/` | the UWP app |
| `app/src/Discovery.*` | LAN discovery and wake-from-rest |
| `app/src/Registration.*` | pairing with the console PIN |
| `app/src/PsnAuth.*` | Sign in with PSN, to get the account ID |
| `app/src/StreamSession.*` | chiaki session, connecting video, audio and input |
| `app/src/VideoDecoder.*` | Media Foundation H.264/HEVC decoder |
| `app/src/VideoRenderer.*` | D3D11 renderer (NV12 → screen via the hardware video processor) |
| `app/src/AudioPlayer.*` | XAudio2 output |
| `app/src/GamepadInput.*` | Xbox controller → DualShock mapping |

## Build it yourself

### 1. Install the tools (Windows PC)

**Visual Studio 2022** (Community is fine), version 17.14 or later. In the Visual Studio Installer, select
**Modify** and add:

- Workload **Universal Windows Platform development**, including the optional
  **C++ (v143) Universal Windows Platform tools**
- Workload **Desktop development with C++**, including the optional
  **C++ Clang tools for Windows** and **vcpkg package manager**
- Individual component **Windows 11 SDK (10.0.22621.0)**

The same from the command line. It must be run from an **administrator** PowerShell; otherwise the installer
exits with code 5007 and installs nothing:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify `
  --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community" `
  --add Microsoft.VisualStudio.Workload.Universal --add Microsoft.VisualStudio.ComponentGroup.UWP.VC `
  --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset `
  --add Microsoft.VisualStudio.Component.Vcpkg --add Microsoft.VisualStudio.Component.Windows11SDK.22621 `
  --passive --norestart
```

Then, from a **Developer PowerShell for VS 2022**, run this once:

```powershell
vcpkg integrate install
```

### 2. Get the code

```powershell
git clone --recurse-submodules https://github.com/yoni101087/psxbox.git
# or, in an existing checkout:
git submodule update --init --depth 1 third_party/chiaki-ng
cd third_party/chiaki-ng
git submodule update --init --depth 1 third-party/nanopb third-party/jerasure third-party/gf-complete
```

### 3. Build

In a Developer PowerShell, from the repo root:

```powershell
msbuild PsXbox.sln -t:restore -p:RestorePackagesConfig=true
msbuild PsXbox.sln -p:Configuration=Release -p:Platform=x64 -m
```

Or open `PsXbox.sln` in Visual Studio, choose **Release | x64**, and build.

- **First build:** about 10 minutes, mostly vcpkg downloading its tools and compiling OpenSSL (about 6 min)
  and Opus for UWP. vcpkg caches them, so later builds take 1–2 minutes.
- **Dependencies:** they come from `vcpkg.json` (pinned with `builtin-baseline`) and the custom triplet
  `triplets/x64-uwp-psxbox.cmake` (static libraries, dynamic CRT). The NuGet package (C++/WinRT) comes from
  nuget.org via `nuget.config`.
- **Signing error:** once the certificate below exists, a plain build ends with "Certificate could not be
  opened" because it doesn't pass the password. That error is harmless; the package build passes it.

You can run the app on your **PC** first (debug target "Local Machine"). Discovery, registration and
streaming work there too, which makes debugging much faster. For HEVC on a PC you need the
"HEVC Video Extensions" from the Store.

### 4. Make the package

Create the signing certificate once. Its subject, `CN=PsXboxDev`, matches the publisher in
`app/Package.appxmanifest`:

```powershell
scripts\make-cert.ps1
```

This writes `app\PsXbox_TemporaryKey.pfx` (password `psxbox`; it's gitignored). Then build the package:

```powershell
msbuild app\PsXbox.vcxproj -p:Configuration=Release -p:Platform=x64 `
  -p:UapAppxPackageBuildMode=SideloadOnly -p:PackageCertificatePassword=psxbox -p:AppxBundle=Never
```

The package lands in `app\AppPackages\PsXbox\PsXbox_<version>_x64_Test\`, together with
`Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx`. Install both as described in
[Install a release](#install-a-release-on-your-xbox). Bump `Version` in `app/Package.appxmanifest` for each
release so the Xbox installs it as an update.

Alternatively, deploy straight from Visual Studio: set the debug target to **Remote Machine**, enter the Xbox
IP, use authentication *Universal (Unencrypted Protocol)*, and press F5. Pair with the PIN from Dev Home if
asked.

## Troubleshooting

- The **log** is at the bottom of the home screen. A full copy is written to
  `LocalState\psxbox.log`, which you can download with Device Portal → File explorer.
- **Console not found:** the Xbox and the PlayStation must be on the same network/subnet. Try
  **Add by IP**. Some routers block broadcast between Wi-Fi and wired ports.
- **Registration failed:** the PIN expires after about 5 minutes; open Link Device / Add Device again for a
  new one. Check that the Account ID belongs to the PSN user signed in on the console.
- **"PSN Account ID isn't valid":** you probably entered your PSN username. Use **Sign in with PSN**, or see
  [About the PSN Account ID](#about-the-psn-account-id).
- **"Login passcode" error:** remove the passcode from the PlayStation user (not supported yet).
- **Stutter:** use wired Ethernet on both consoles, set the app type to Game, or try 720p / a lower
  bitrate in Settings.

## Not done yet / ideas

- Remote Play over the internet (needs curl, json-c and libevent built for UWP; see `stubs/remote_stubs.c`)
- HDR (HEVC Main10 / P010 output), 4K on Series X
- On-screen touchpad, motion controls, microphone, in-stream on-screen keyboard
- PS4 login passcode prompt, and an in-stream menu with stats (bitrate, packet loss, latency)
- Xbox Media Remote support

## License

AGPL-3.0 (with the OpenSSL exception), the same as chiaki-ng, because this links against it; see
`LICENSE`. If you share builds, you must also share the source. Not affiliated with Sony or Microsoft.
"PlayStation", "PS4" and "PS5" are trademarks of Sony Interactive Entertainment.
