# Changelog

## v0.1.0 — 2025-XX-XX
- Initial Smart Gamma shader filter implementation
- GPU-based luminance probe + temporal smoothing + fade state machine
- OBS UI + locale strings for all parameters
- Cross-platform CMake project and GitHub Actions builds for macOS + Windows
- Packaging script and documentation
- Fix Windows CI build by disabling the OBS UI/frontend stack when compiling
  the libobs dependency, pointing Smart Gamma’s build to the generated
  `libobsConfig.cmake`, and explicitly exposing the w32-pthreads package from
  the OBS build tree
