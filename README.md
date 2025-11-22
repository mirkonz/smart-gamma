# Smart Gamma — OBS Shader Filter

**Smart Gamma** is a single-pass GPU shader filter for OBS Studio that watches scene luminance and automatically boosts gamma/brightness/contrast when gameplay turns dark, then fades back out once things brighten up.

## Highlights
- GPU-first design: one shader pass with no per-frame heap churn and no GPU readbacks larger than 32×32.
- Dynamic activation: configurable luminance threshold, hold time, and smooth fade-in/out envelopes.
- Tunable adjustments: gamma boost, brightness offset, contrast recovery, and optional saturation tweak with sane defaults.
- Cross-platform by design: builds on macOS (Apple Silicon + Intel) and Windows 64-bit from the same CMake project.
- OBS-native UI & localization so settings show up exactly where creators expect them.

## Platforms & Requirements
- **Supported OS:** macOS 13+ (Intel & Apple Silicon), Windows 10/11 64-bit.
- **OBS Studio:** 30.1 or newer (needs the modern libobs shader stack).
- **GPU:** Any device that runs OBS Studio with shader filters enabled.
- **License:** MIT (see `LICENSE`).

## Parameter Reference
Parameters are defined once in [`include/smart-gamma/parameter_schema.hpp`](include/smart-gamma/parameter_schema.hpp) and documented in [`docs/parameters.md`](docs/parameters.md). These values are reused in code, the UI, and the docs to keep everything DRY.

| Setting | Default | Notes |
| --- | --- | --- |
| Darkness threshold | `0.35` | Normalized average luminance required to trigger Smart Gamma. |
| Threshold duration (ms) | `600` | Time the scene must stay below (to fade in) or above (to fade back out) the darkness threshold before Smart Gamma reacts. |
| Fade in (ms) | `200` | Duration of the fade from 0 to full strength once active. |
| Fade out (ms) | `450` | Duration of the fade when returning to normal brightness. |
| Gamma boost | `1.20` | Gamma multiplier applied while active. |
| Brightness offset | `0.10` | Linear brightness offset (use small values to avoid clipping). |
| Contrast | `1.10` | Contrast gain to keep highlights alive after the gamma boost. |
| Saturation | `1.00` | Optional saturation multiplier while the effect is active. |

### Using Smart Gamma
- **Default behavior:** Smart Gamma does nothing until the average scene luminance falls below the darkness threshold for at least the threshold duration, and it waits the same duration above the threshold before fading back out. When you manually adjust gamma/brightness/contrast/saturation in the filter UI, they still take effect only while the automatic `effect_strength` scalar is non-zero, so the preview stays faithful when the environment is bright.
- **Slider guidance:**
  - **Darkness threshold:** Lower values make the filter trigger only on very dark scenes; raise it to catch dim but not fully black footage.
  - **Threshold duration:** This is how long the scene must remain below the threshold before the filter starts fading in and how long it must stay above before fading out; setting it near 0 makes Smart Gamma responsive, while 20,000 ms (20 s) makes it very patient.
  - **Fade in / Fade out:** These up-to-20 s envelopes control how smooth the transition is. Keep fade-in short to react quickly or long to avoid popping, fade-out longer if you want the correction to linger.
  - **Gamma / Brightness / Contrast / Saturation:** Tune these only for the boosted state; the values blend from zero as `effect_strength` ramps from 0 → 1. High increases can quickly wash out highlights.

## How It Works
1. **Luminance probe:** Each frame the filter downsamples the source texture to 32×32 on the GPU, stages that tiny texture once, and computes the Rec.709 luminance average. An exponential moving average (α = 0.18) keeps the signal stable.
2. **State machine:** IDLE → WAITING → FADING_IN → ACTIVE → FADING_OUT transitions control a single `effect_strength` scalar (0–1). Threshold crossings during fades behave gracefully (brightening in FADING_IN immediately pivots to FADING_OUT, etc.).
3. **Shader blend:** The shader file at `data/shaders/smart-gamma.effect` applies gamma/brightness/contrast/saturation adjustments and lerps with the original frame based on `effect_strength`. Strength 0 returns the untouched frame; strength 1 applies the full correction.

## Building from Source
Smart Gamma now mirrors the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) layout. `buildspec.json` pins the OBS/libobs + dependency revisions and the helper modules in `cmake/` wire them up automatically, so building only requires choosing the preset that matches your host OS. The first configure run downloads everything into `.deps/`.

### macOS (Xcode 16+)
```bash
cmake --preset macos
cmake --build --preset macos
cmake --install build_macos --config RelWithDebInfo
```
The preset emits a universal `smart-gamma.plugin` bundle. macOS defaults install directly into `~/Library/Application Support/obs-studio/plugins`, and the install step also generates a distributable `smart-gamma.pkg` alongside the prefix. Use `scripts/link-macos.sh` only if you install elsewhere and still want a symlink back into the OBS plug-ins folder.

### Windows (Visual Studio 2022)
```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix dist/windows-x64
```
This preset configures the right toolset, pulls obs-deps + Qt6, and installs into `dist/windows-x64/smart-gamma`. Copy that tree into `%APPDATA%/obs-studio/plugins` or your OBS install directory.

### Ubuntu 24.04 / Linux
```bash
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
cmake --install build_x86_64 --prefix dist/linux-x86_64
```
Linux builds use Ninja with `RelWithDebInfo` and install the `.so` into `dist/linux-x86_64/lib/obs-plugins` plus data under `share/obs/obs-plugins/smart-gamma`.

### Tips
- Need automation-friendly settings? Use the `macos-ci`, `windows-ci-x64`, or `ubuntu-ci-x86_64` presets to enable warnings-as-errors and ccache.
- Flip `-DENABLE_FRONTEND_API=ON` / `-DENABLE_QT=ON` if you add UI bits; the helper modules will locate the extra SDKs.
- Delete `.deps/` to force a clean dependency download between OBS releases.

### Packaging
- macOS: the install step already emits `smart-gamma.pkg`; wrap it in a DMG with `scripts/create-dmg.sh "<install-prefix>" dist/smart-gamma-macos.dmg` if desired.
- Windows: `powershell ./scripts/create-windows-installer.ps1 -SourceDir dist/windows-x64 -OutputExe dist/smart-gamma-windows.exe`.
- Any platform: `scripts/package.sh <build-dir> <platform-tag> [output-dir]` zips an install tree (e.g., `scripts/package.sh build_x86_64 linux-x86_64 dist`).

## Testing Checklist
Create three quick OBS scenes to validate behaviour.
- **Dark gameplay:** mostly black gameplay footage, confirm activation delay + fade-in feel smooth.
- **Mixed lighting:** capture with flashing HUDs to ensure smoothing avoids flicker.
- **Strobe/Explosions:** verify fade-out path keeps up with rapid brightness spikes.
Monitor the OBS stats dock; Smart Gamma should stay under 0.1 ms/frame at 1080p on modern GPUs.

## Continuous Integration
The new template-driven workflows under `.github/workflows/` (`push.yaml`, `pr-pull.yaml`, and `dispatch.yaml`) call into `build-project.yaml` and `check-format.yaml`, so CI reuses the exact presets listed above to fetch dependencies, build macOS/Windows/Ubuntu artifacts, and run clang-format + gersemi.

## Roadmap
See [`plan.md`](plan.md) for future shader-focused tasks (per-source profiles, calibration wizard, HUD masking, ML experiments, etc.). Feel free to PR improvements there.
