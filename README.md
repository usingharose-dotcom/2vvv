# YOLO Tune: VST3 / AU plugin

Made by **Kinok9** (graphic designer), **Ohjokr** (scripter) and **Razee** (manager).

This folder is the full source code for the YOLO Tune pitch-correction plugin. Building it gives you:

- **VST3** for Windows and Mac (FL Studio, Ableton, Cubase, Reaper, Studio One, Bitwig…)
- **AU** for Mac (Logic, GarageBand)
- **Standalone app** that tunes your mic live

The plugin window is the same YOLO interface as the web version. The audio engine is the same tuner, rewritten in C++.

---

## Easiest way: let GitHub build it (no coding tools needed)

1. Make a free account at github.com and create a **new repository** (Private is fine).
2. On the new repo page, click **"uploading an existing file"**. Drag in **everything inside this folder**,
   including the hidden `.github` folder. (If your computer hides it, use GitHub Desktop or
   turn on "show hidden files".) Click **Commit**.
3. Open the **Actions** tab. A build called *Build YOLO Tune* starts by itself and takes about 10–15 minutes.
4. When it shows a green check, click it and scroll to **Artifacts**. Download
   **YOLO-Tune-Windows** and/or **YOLO-Tune-macOS**.
5. Unzip the download and follow `INSTALL.txt`.

## Building on your own computer

You need **CMake 3.22+** and a C++ compiler. JUCE downloads automatically the first time.

### Windows (Visual Studio 2022 with "Desktop development with C++")
In PowerShell, run this once to get the WebView2 SDK:
```
Register-PackageSource -provider NuGet -name nugetRepository -location https://www.nuget.org/api/v2
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.1901.177 -Source nugetRepository
```
Then, in this folder:
```
cmake -B build
cmake --build build --config Release
```

### Mac (Xcode installed)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin files end up in `build/YOLO_artefacts/Release/` (`VST3/`, `AU/`, `Standalone/`).

---

## What's inside

| File | What it does |
|---|---|
| `Source/YoloDSP.h` | The tuner: pitch detection, note snapping, retune speed, flex-tune, humanize, doubler, guide tone |
| `Source/PluginProcessor.*` | Plugin parameters (all automatable), audio routing, latency, saving with your project |
| `Source/PluginEditor.*` | Plugin window: shows `ui/index.html` and connects it to the audio engine |
| `ui/index.html` | The YOLO interface, with fonts built in so it works offline |
| `.github/workflows/build.yml` | Automatic Windows + Mac builds on GitHub |
| `tests/` | Checks that the C++ tuner gives the same output as the browser version |

### How it differs from the web version
- Audio comes from the DAW track, so the **Live Mic** and **Test file** buttons are hidden in the plugin.
  The standalone app has its own audio/mic settings.
- **Every knob, slider, switch and note button is a DAW parameter**: you can automate it,
  and it's saved with your project.
- Key/scale, groups 1–4, A/B and other view settings are also saved in the project.
- **User presets** are saved to a file, so they show up in every project and every instance:
  - Windows: `%APPDATA%\YOLO Tune\presets.json`
  - Mac: `~/Library/YOLO Tune/presets.json`
- **Bypass** is latency-compensated (the dry signal stays lined up with your other tracks).
- **Tone Volume** mixes a sine wave on the corrected note into the output. It only sounds while you're singing.

### Checking the tuner matches the web version
```
g++ -O2 -std=c++17 tests/dsp_test.cpp -o tests/dsp_test
cd tests && node compare.js ../ui/index.html
```
Each line should show a `maxDiff` of about 1e-7 or less.
