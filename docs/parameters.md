# Smart Gamma Parameter Reference

Canonical definitions for every setting exposed by the Smart Gamma filter. The same keys and defaults are used by the OBS UI (`smart-gamma/parameter_schema.hpp`) and the README table, so this is the single source of truth. The Mode dropdown (`smart_gamma_mode`) lives outside the schema because it is an enum, but it is documented alongside the slider-based parameters below.

| Setting | OBS Key | Range | Default | Description |
| --- | --- | --- | --- | --- |
| Mode | `smart_gamma_mode` | Auto brightness / Threshold fade | Auto brightness | Auto brightness scales effect strength based on how far below the darkness threshold the scene sits. Threshold fade restores the manual trigger → hold → fade workflow (and exposes the timing sliders below). |
| Darkness threshold | `darkness_threshold` | 0 – 100 % | 35 % | Average normalized luminance (expressed as a percentage between pure black and pure white) used as the point where Auto brightness begins scaling or Threshold fade can trigger. |
| Threshold duration | `activation_delay_ms` | 0 – 20000 ms | 600 ms | Minimum amount of time the scene must remain below the darkness threshold before fading in or above it before fading out; only evaluated in Threshold fade mode. |
| Fade in | `fade_in_ms` | 0 – 20000 ms | 200 ms | Duration of the fade from 0 to full effect once the scene is dark enough; applies to Threshold fade mode. |
| Fade out | `fade_out_ms` | 0 – 20000 ms | 450 ms | Duration of the fade from full effect back to zero after the scene brightens; applies to Threshold fade mode. |
| Gamma boost | `gamma` | 0.5 – 3.0 | 1.20 | Gamma multiplier at full strength (higher values raise shadows). Auto brightness ramps toward this as scenes darken. |
| Brightness offset | `brightness` | -0.5 – 0.5 | 0.10 | Linear brightness offset applied at full strength; keep this modest to avoid clipping. |
| Contrast | `contrast` | 0.5 – 2.0 | 1.10 | Contrast gain applied alongside gamma to maintain highlight separation once the effect is fully engaged. |
| Saturation | `saturation` | 0.0 – 2.5 | 1.00 | Optional saturation multiplier that kicks in as the effect ramps up. |
