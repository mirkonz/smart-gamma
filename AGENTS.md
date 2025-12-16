# AGENTS — Smart Gamma

This repository contains the Smart Gamma OBS shader filter. The plugin downsamples the current scene to probe luminance, drives a lightweight CPU state machine, and feeds a single shader in `data/shaders/smart-gamma.effect` that applies gamma/brightness/contrast/saturation adjustments. Most work happens inside `src/smart-gamma-plugin.cpp`, with parameter defaults declared in `include/smart-gamma/parameter_schema.hpp`.

## Repository Map
- `src/`: Main C++ implementation of the OBS filter (module entry points, luminance sampling, state machine, shader bindings).
- `include/smart-gamma/`: Canonical parameter descriptors used across the UI, docs, and runtime.
- `data/shaders/`: GPU `.effect` files loaded by the filter. Keep them in sync with the uniforms in `src/`.
- `data/locale/en-US.ini`: Localization strings. Every UI label/description should have a matching token here.
- `docs/parameters.md`: Markdown version of the parameter table. Update this whenever defaults or ranges shift.
- `cmake/`, `CMakeLists.txt`, `CMakePresets.json`, `buildspec.json`: Build system wiring via the obs-plugintemplate bootstrap.
- `build-aux/`: Formatting helpers (`run-clang-format`, `run-gersemi`, `run-swift-format`).
- `scripts/`: Packaging helpers (macOS pkg/DMG, Windows installer, Linux zip).
- `plan.md`: Living roadmap for future shader/state-machine work—update it when you complete scoped tasks.

## Build & Test Playbook
1. **Configure** with the preset that matches your host:
   - macOS: `cmake --preset macos`
   - Windows: `cmake --preset windows-x64`
   - Ubuntu: `cmake --preset ubuntu-x86_64`
2. **Build** via `cmake --build --preset <preset-name>` (defaults to `RelWithDebInfo`).
3. **Install** into the OBS plug-in tree if you need to run OBS locally:
   - macOS: `cmake --install build_macos --config RelWithDebInfo`
   - Windows: `cmake --install build_x64 --config RelWithDebInfo --prefix dist/windows-x64`
   - Ubuntu: `cmake --install build_x86_64 --prefix dist/linux-x86_64`
4. **Manual validation** still matters. Follow the scenarios in `README.md` (“Dark gameplay”, “Mixed lighting”, “Strobe/Explosions”) while watching OBS’ stats dock for frame-time regressions.

## Formatting & Static Checks
- **C/C++/ObjC**: Run `./build-aux/run-clang-format` (or `./build-aux/run-clang-format --check` in CI mode). The script requires `clang-format` **19.1.1** exactly. Install it locally via `brew install obsproject/tools/clang-format@19` (or ensure `/opt/homebrew/opt/clang-format@19/bin` is on `PATH`). Avoid formatting `.deps/` or generated bundles.
- **CMake**: Run `./build-aux/run-gersemi` (install `gersemi` ≥ 0.12.0 via `pipx install gersemi==0.12.0` or Homebrew). The config lives in `.gersemirc`.
- **Swift**: `./build-aux/run-swift-format` exists but is not needed until Swift sources are added.
- **Editor defaults**: VS Code is set to use `/opt/homebrew/bin/clang-format` with `.clang-format`. Do not rewrap comments manually—let the formatter handle it.

## Parameters, Localization & Docs Stay in Lockstep
Whenever you change user-facing settings, update **all** of the following:
1. `include/smart-gamma/parameter_schema.hpp` — single source of truth for ranges, defaults, and OBS keys.
2. `data/locale/en-US.ini` — add or update `SmartGamma.Param.*` tokens.
3. `docs/parameters.md` — regenerate the Markdown table so docs match the code.
4. `README.md` — keep the “Parameter Reference” table aligned with `docs/parameters.md`.

Mode labels (`SmartGamma.Param.Mode.*`) and tooltips live alongside the slider definitions, while shader uniforms must stay synchronized with `src/smart-gamma-plugin.cpp` and `data/shaders/smart-gamma.effect`.

## Common Pitfalls & Expectations
- **Shader path**: Use `obs_module_file("shaders/...")` to locate bundled effects; never hardcode filesystem paths.
- **Luminance sampling**: The downsample path must stay cheap (32×32) and reuse staging surfaces. Avoid per-frame heap allocations or blocking `gs_stage_texture`.
- **State machine**: Respect the existing states in `SmartGammaState`. Any new transitions should maintain the `effect_strength` invariant (0–1, monotonic within each state).
- **HDR/BGRA formats**: When touching pixel unpacking, keep the half-float/format helpers untouched unless you add exhaustive test coverage.
- **Plan maintenance**: If your work completes a task in `plan.md`, mark it with `[x]` and add subtasks if scope grows.

Stick to C++17, prefer std facilities already in use (`std::array`, `std::atomic`, `std::unique_ptr`), and keep shader math branch-light. Running the formatters plus a preset build should be the final step before handing work back to the user.
