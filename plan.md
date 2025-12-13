# Smart Gamma — Shader Filter OBS Plugin Autonomous Build Plan

This file instructs autonomous AI agents and human developers on how to build the **Smart Gamma** OBS shader filter plugin without needing external context.

**Smart Gamma**: a *GPU-based* dynamic gamma/brightness/contrast/saturation filter for OBS that automatically boosts visibility when scenes become dark, with smooth fades and user-controlled thresholds. It is implemented as a **shader filter**, optimized for performance and cross-platform builds (macOS + Windows).

---

## 🧩 How Agents Should Use This Plan

1. Read the entire plan from top to bottom at least once.
2. Locate the **first unchecked task** (`- [ ]`).
3. Complete that task:
   - Generate code, configuration, documentation, or other artifacts as needed.
4. After completing it, mark the task as complete by changing `- [ ]` → `- [x]`.
5. Do **not** delete tasks; only mark them complete. If necessary, add sub-tasks underneath.
6. If a task is vague, create more detailed sub-tasks directly under it and complete those.
7. Always use this plan as the **single source of truth**. Do not assume information that is not present here.
8. Only restructure entire sections if explicitly requested by the user or a higher-level controller.
9. **Follow DRY (Don’t Repeat Yourself)** — avoid duplicating tasks or explanations. If a concept is defined once, reference it instead of rewriting it.
10. When in doubt, prioritize:
    - Single-pass GPU shader design
    - Cross-platform support (macOS + Windows)
    - Real-time performance for game streaming (60–240 FPS)
    - Simple, understandable controls for end-users.

---

## ✔️ Status Legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[?]` Needs clarification or design decision

---

# 🏗 Section 1 — Project Overview & Architecture Choice

- [x] Confirm plugin display name: **Smart Gamma**
- [x] Confirm repo name: **smart-gamma**
- [x] Write one-sentence elevator pitch for README
- [x] Define target platforms:
  - [x] macOS (Apple Silicon + Intel if possible)
  - [x] Windows 64-bit
- [x] Define minimum OBS version supported
- [x] Decide on license (e.g., MIT, BSD-2, Apache-2.0)
- [x] Confirm architecture:
  - [x] Implement as **shader filter plugin** (GPU-based, single-pass)
  - [x] CPU logic limited to luminance detection + state machine + parameter updates

---

# 📁 Section 2 — Repository & Project Setup

- [x] Create GitHub repository `smart-gamma`
- [x] Initialize repository with:
  - [x] `README.md`
  - [x] `LICENSE`
  - [x] `.gitignore`
- [x] Create base folder structure:
  - [x] `/src`
  - [x] `/include`
  - [x] `/data`
  - [x] `/data/shaders`
  - [x] `/data/locale`
  - [x] `/cmake`
  - [x] `/scripts`
- [x] Add `.gitkeep` placeholders for empty folders to ensure Git tracks structure
- [x] Add minimal `CMakeLists.txt` for OBS plugin (no unused helpers; DRY)
- [x] Adopt the upstream `obs-plugintemplate` bootstrap (shared `buildspec.json`, presets, helper modules) so macOS/Windows/Linux builds share the same dependency/download flow
- [x] Add OBS module skeleton:
  - [x] Module name & description
  - [x] `obs_module_load` / `obs_module_unload`
  - [x] Default locale registration (if used)

---

# ⚙️ Section 3 — Core Behaviour, Parameters & Performance Requirements

- [x] Define dynamic activation behaviour:
  - [x] Filter only activates when scene luminance is below a threshold **for a minimum duration**
  - [x] Filter fades in over a configurable duration
  - [x] Filter fades out smoothly when scene brightens again
- [x] Define user-facing parameters (document once, reused in UI + code):
  - [x] Darkness threshold (0–100% or normalized 0–1)
  - [x] Time below threshold before activation (ms)
  - [x] Fade-in duration (ms)
  - [x] Fade-out duration (ms)
  - [x] Gamma adjustment
  - [x] Brightness adjustment
  - [x] Contrast adjustment
  - [x] Optional saturation adjustment
- [x] Set sane default values and ranges
- [x] Record all parameter definitions in a single reference (for DRY reuse in UI and docs)
- [x] Define performance goals:
  - [x] Single shader pass
  - [x] ≤ 0.1 ms per frame at 1080p target; scalable to 1440p / 4K
  - [x] No per-frame heap allocations
  - [x] No blocking GPU readbacks on main thread
  - [x] Shared state stored in lightweight structs

---

# 🧮 Section 4 — Luminance Detection (GPU-First, CPU-Light)

- [x] Decide luminance detection approach:
  - [x] **Preferred**: GPU-based downsample pass (small texture) → average luminance
  - [x] **Fallback**: CPU readback of small downsampled texture
- [x] Implement luminance computation:
  - [x] Use Rec. 709 luma (e.g., `Y = 0.2126 R + 0.7152 G + 0.0722 B`)
  - [x] Compute average or weighted average luminance
- [x] Add temporal smoothing:
  - [x] Implement exponential moving average over frames
  - [x] Make smoothing strength configurable or fixed
- [x] Implement threshold logic:
  - [x] Compare smoothed luminance vs. threshold
  - [x] Track time spent below threshold
  - [x] Only signal “dark enough” after configured delay
- [x] Ensure luminance detection runs with:
  - [x] Minimal texture size
  - [x] Minimal CPU work
  - [x] No full-resolution readbacks

---

# 🌈 Section 5 — Smart Gamma Shader Design (Single-Pass, GPU-Based)

- [x] Specify the combined adjustment formula applied per pixel:
  - [x] Gamma correction (using fast, GPU-friendly method)
  - [x] Brightness offset
  - [x] Contrast adjustment
  - [x] Optional saturation adjustment
- [x] Implement a single **Smart Gamma** shader file in `/data/shaders`:
  - [x] Inputs:
    - [x] Source texture
    - [x] Effect strength (0–1 scalar)
    - [x] Gamma, brightness, contrast, saturation parameters
  - [x] Output:
    - [x] Adjusted color
- [x] Ensure the shader:
  - [x] Avoids branching where possible
  - [x] Uses efficient math (avoid expensive `pow` where approximations suffice)
  - [x] Works with OBS’s graphics backends (GL, D3D, Metal via OBS)
- [x] Validate that:
  - [x] All color adjustments are applied in one pass
  - [x] Effect strength = 0 yields original image
  - [x] Effect strength = 1 yields fully adjusted image

---

# 🔄 Section 6 — State Machine & Timing Logic (CPU-Side, Lightweight)

- [x] Define states:
  - [x] IDLE (effect strength = 0)
  - [x] WAITING_FOR_THRESHOLD (tracking time below threshold)
  - [x] FADING_IN (interpolating effect strength 0 → 1)
  - [x] ACTIVE (effect strength = 1)
  - [x] FADING_OUT (interpolating effect strength 1 → 0)
- [x] Implement timers for:
  - [x] Time-below-threshold before activation
  - [x] Fade-in duration
  - [x] Fade-out duration
- [x] Implement transitions (define once, DRY):
  - [x] Light → Dark: IDLE → WAITING_FOR_THRESHOLD → FADING_IN → ACTIVE
  - [x] Dark → Light: ACTIVE → FADING_OUT → IDLE
- [x] Handle rapid luminance changes:
  - [x] If it brightens during WAITING_FOR_THRESHOLD → return to IDLE
  - [x] If it brightens during FADING_IN → start FADING_OUT from current strength
- [x] Expose a single scalar `effect_strength` (0–1) to the shader each frame
- [x] Ensure state machine updates are O(1) per frame with minimal branching

---

# 🖥 Section 7 — OBS Shader Filter Integration & UI

- [x] Define an `obs_source_info` (or appropriate OBS struct) representing the Smart Gamma shader filter
- [x] Register the filter in `obs_module_load`
- [x] Implement filter callbacks:
  - [x] `get_name`
  - [x] `create` / `destroy`
  - [x] `get_properties` (for UI sliders/settings)
  - [x] `update` (read settings into internal state)
  - [x] `video_render` / `filter_video` to:
    - [x] Ensure luminance detection is updated
    - [x] Update state machine / effect_strength
    - [x] Bind shader and parameters
- [x] Implement UI properties (once, reused in docs):
  - [x] Darkness threshold slider
  - [x] Time-below-threshold delay
  - [x] Fade-in / fade-out durations
  - [x] Gamma slider
  - [x] Brightness slider
  - [x] Contrast slider
  - [x] Optional saturation slider
- [x] Add tooltips for each property
- [x] Ensure UI changes are immediately reflected in preview

---

# 🧪 Section 8 — macOS Build & Local Testing (Development Platform)

- [x] Configure CMake build on macOS via the `macos` preset (pulls pinned deps into `.deps/` automatically)
- [x] Build/install the universal `smart-gamma.plugin` bundle emitted by the template toolchain
- [ ] Install plugin into local OBS plugin directory on macOS
- [ ] Create test scenes:
  - [ ] Very dark game footage
  - [ ] Mixed brightness game content
  - [ ] Rapidly changing lighting (flashes, explosions)
- [ ] Validate:
  - [ ] Luminance detection accuracy
  - [ ] Threshold behaviour
  - [ ] Fades feel smooth and non-jarring
  - [ ] No visible banding or artifacts
  - [ ] Performance impact (monitor FPS and GPU usage)

---

# 🧰 Section 9 — Cross-Platform CI (macOS + Windows + Ubuntu Shader Filter Builds)

- [x] Generalize CMake configuration to support:
  - [x] macOS builds (Xcode presets)
  - [x] Windows builds (Visual Studio presets)
  - [x] Ubuntu builds (Ninja presets)
- [x] Adopt the template GitHub Actions workflows so CI fans out to macOS, Windows, and Ubuntu using the shared composite actions + build scripts (with packaging/codesigning hooks baked in)
- [ ] Confirm artifacts install and run correctly on:
  - [ ] Local macOS OBS install
  - [ ] Windows OBS install

---

# 📦 Section 10 — Packaging, Documentation & Release

- [x] Write detailed `README.md`:
  - [x] Overview and features
  - [x] Installation (macOS + Windows)
  - [x] Usage section explaining thresholds, fades, and tuning
  - [x] Performance notes / recommended settings
- [x] Add `CHANGELOG.md` with initial version entry
- [x] Decide on versioning (e.g., SemVer)
- [x] Prepare `.zip` packages:
  - [x] macOS: correct plugin folder structure
  - [x] Windows: `.dll` + correct plugin folder structure
- [x] Configure GitHub Release:
  - [x] Tag (e.g., `v0.1.0`)
  - [x] Attach macOS + Windows artifacts
  - [x] Include short changelog / highlights

---

# 🔁 Section 11 — Future Enhancements (Shader-First Evolution)

- [ ] Per-scene / per-source Smart Gamma profiles
- [ ] Auto-calibration wizard that measures game brightness and suggests defaults
- [ ] Advanced gamma/contrast curve editor in UI
- [ ] More sophisticated luminance analysis (e.g., exclude HUD regions)
- [ ] Optional ML-based dark scene detection (research item)
