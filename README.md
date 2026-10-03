# PS Xbox

A PlayStation Remote Play client for Xbox (UWP, Dev Mode), in the spirit of PXPlay.
Stream your PS4 or PS5 to an Xbox One / Series X|S on your local network and play it with the Xbox controller.

It's built on the protocol library from [chiaki-ng](https://github.com/streetpea/chiaki-ng),
the open-source, reverse-engineered implementation of Sony's Remote Play protocol.

> **Status: first version, never compiled.** Everything is written but hasn't been built or
> tested on an Xbox yet. Expect a round of build fixes before it runs.

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
| `app/src/StreamSession.*` | chiaki session, connecting video, audio and input |
| `app/src/VideoDecoder.*` | Media Foundation H.264/HEVC decoder |
| `app/src/VideoRenderer.*` | D3D11 renderer (NV12 → screen via the hardware video processor) |
| `app/src/AudioPlayer.*` | XAudio2 output |
| `app/src/GamepadInput.*` | Xbox controller → DualShock mapping |

## 1. Install the tools (Windows PC)

**Visual Studio 2022** (Community is fine). In the installer, select:

- Workload **Universal Windows Platform development**, including the optional
  **C++ (v143) Universal Windows Platform tools**
- Workload **Desktop development with C++**, including the optional
  **C++ Clang tools for Windows** and **vcpkg package manager**
- A **Windows 11 SDK (10.0.22621)**

Then, from a *Developer PowerShell for VS 2022*, run this once:

```powershell
vcpkg integrate install
```

## 2. Get the code

```powershell
git clone --recurse-submodules <your repo url> psxbox
# or, in an existing checkout:
git submodule update --init --recursive --depth 1 third_party/chiaki-ng
cd third_party/chiaki-ng
git submodule update --init --depth 1 third-party/nanopb third-party/jerasure third-party/gf-complete
```

## 3. Build

Open `PsXbox.sln`, choose **Release | x64**, and build. On the first build, vcpkg compiles OpenSSL and
Opus for UWP, which takes a few minutes.

Or from the command line (Developer PowerShell):

```powershell
msbuild PsXbox.sln -t:restore -p:RestorePackagesConfig=true
msbuild PsXbox.sln -p:Configuration=Release -p:Platform=x64
```

You can run the app on your **PC** first (debug target "Local Machine"). Discovery, registration and
streaming work there too, which makes debugging much faster. For HEVC on a PC you need the
"HEVC Video Extensions" from the Store.

## 4. Put your Xbox in Dev Mode and deploy

1. On the Xbox, install the **Xbox Dev Mode** app from the Store. Activate it with the code it shows,
   in Partner Center (your developer account), then switch to Dev Mode.
2. In Dev Home on the Xbox, note the IP address and enable **Remote Access** (Device Portal).
3. Deploy, using one of these:
   - **Visual Studio:** set the debug target to **Remote Machine**, enter the Xbox IP, use authentication
     *Universal (Unencrypted Protocol)*, and press F5. Pair with the PIN from Dev Home if asked.
   - **Package:** run `scripts\make-cert.ps1`, then build with
     `msbuild app\PsXbox.vcxproj -p:Configuration=Release -p:Platform=x64 -p:UapAppxPackageBuildMode=SideloadOnly -p:PackageCertificatePassword=psxbox`.
     Upload the `.msix`/`.appx` from `AppPackages\` (plus its `Dependencies\x64\*.appx`) at
     `http://<xbox-ip>:11443` → *Add*.
4. **Important:** in Dev Home, select PS Xbox → *View details* → change **App type** from *App* to **Game**.
   Apps only get a fraction of the GPU and memory, while games get the full console.

## 5. First connection

1. **Turn on Remote Play on the PlayStation:**
   - PS5: Settings → System → Remote Play → enable.
   - PS4: Settings → Remote Play Connection Settings → enable. Also set it as your primary PS4 if you
     want wake from rest mode.
2. **Get your PSN Account ID (base64).** This is not your online ID. On a PC:
   ```powershell
   pip install requests
   python third_party\chiaki-ng\scripts\psn-account-id.py
   ```
   Sign in with the link it prints, then paste the redirect URL back. It prints something like
   `AbCdEfGhIjk=`. Put it in **Settings** in the app. It's stored on the Xbox.
3. In the app, select your console (it should show up automatically; if not, use **Add by IP**).
4. On the PlayStation open **Link Device** (PS5) or **Add Device** (PS4), enter the 8-digit PIN in the
   app, and select **Register**.
5. Select the registered console to stream. If it's in rest mode, the app wakes it and connects
   once it's ready.

## Controls

| Xbox | PlayStation |
|---|---|
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB, LT / RT | L1 / R1, L2 / R2 (analog) |
| Sticks, stick clicks | Sticks, L3 / R3 |
| D-pad | D-pad |
| Menu | Options |
| View | Touchpad click |
| View + Menu | PS button |
| View + LB | Share / Create |
| **View + Menu + LB + RB** | **Leave the stream** |

Rumble is forwarded to the Xbox controller.

## Troubleshooting

- The **log** is at the bottom of the home screen. A full copy is written to
  `LocalState\psxbox.log`, which you can download with Device Portal → File explorer.
- **Console not found:** the Xbox and the PlayStation must be on the same network/subnet. Try
  **Add by IP**. Some routers block broadcast between Wi-Fi and wired ports.
- **Registration failed:** the PIN expires after about 5 minutes. Check that the Account ID is the
  base64 one for the PSN user signed in on the console.
- **"Login passcode" error:** remove the passcode from the PlayStation user (not supported yet).
- **Stutter:** use wired Ethernet on both consoles, set the app type to Game, or try 720p / a lower
  bitrate in Settings.

## Not done yet / ideas

- PSN sign-in inside the app (instead of the Account ID script)
- Remote Play over the internet (needs curl, json-c and libevent built for UWP; see `stubs/remote_stubs.c`)
- HDR (HEVC Main10 / P010 output), 4K on Series X
- On-screen touchpad, motion controls, microphone, in-stream on-screen keyboard
- PS4 login passcode prompt, and an in-stream menu with stats (bitrate, packet loss, latency)

## License

AGPL-3.0 (with the OpenSSL exception), the same as chiaki-ng, because this links against it; see
`LICENSE`. If you share builds, you must also share the source. Not affiliated with Sony or Microsoft.
"PlayStation", "PS4" and "PS5" are trademarks of Sony Interactive Entertainment.
