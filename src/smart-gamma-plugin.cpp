#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>
#include <obs-module.h>
#include <obs-properties.h>
#include <util/platform.h>

#include "smart-gamma/parameter_schema.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("smart-gamma", "en-US")

#ifndef SMART_GAMMA_VERSION
#define SMART_GAMMA_VERSION "0.0.0"
#endif

#ifndef SMART_GAMMA_AUTHOR
#define SMART_GAMMA_AUTHOR "Unknown author"
#endif

#ifndef SMART_GAMMA_REPO
#define SMART_GAMMA_REPO "https://github.com/obsproject/obs-studio"
#endif

constexpr char kAuthorWebsite[] = "https://mirko.nz";
constexpr char kDarknessThresholdPercentKey[] = "darkness_threshold_is_percent";

constexpr uint32_t kDefaultDownsampleSize = 32;
constexpr float kLuminanceSmoothing = 0.18f;
constexpr float kLuminanceSampleIntervalSeconds = 1.0f / 20.0f;
constexpr float kEpsilon = 1e-4f;

namespace {

enum class SmartGammaState {
	Idle,
	WaitingForThreshold,
	FadingIn,
	Active,
	FadingOut,
};

struct SmartGammaSettings {
	float darkness_threshold =
		static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::DarknessThreshold)) / 100.0f;
	float threshold_duration_ms =
		static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::ThresholdDurationMs));
	float fade_in_ms = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::FadeInMs));
	float fade_out_ms = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::FadeOutMs));
	float gamma = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::Gamma));
	float brightness = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::Brightness));
	float contrast = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::Contrast));
	float saturation = static_cast<float>(smart_gamma::DefaultValue(smart_gamma::Parameter::Saturation));
};

struct SmartGammaFilter {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *strength_param = nullptr;
	gs_eparam_t *gamma_param = nullptr;
	gs_eparam_t *brightness_param = nullptr;
	gs_eparam_t *contrast_param = nullptr;
	gs_eparam_t *saturation_param = nullptr;

	gs_texrender_t *downsample_render = nullptr;
	gs_stagesurf_t *downsample_stage = nullptr;
	uint32_t downsample_size = kDefaultDownsampleSize;
	enum gs_color_format downsample_format = GS_RGBA;

	SmartGammaSettings settings;
	SmartGammaState state = SmartGammaState::Idle;
	float effect_strength = 0.0f;
	float smoothed_luminance = 1.0f;
	float latest_luminance = 1.0f;
	float pending_tick_delta = 0.0f;
	float time_below_threshold = 0.0f;
	float time_above_threshold = 0.0f;
	bool luminance_initialized = false;
	float time_since_last_sample = 0.0f;
	std::atomic<float> displayed_luminance_percent{100.0f};
	float last_properties_update_percent = -1.0f;
};

inline float clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

inline float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

float HalfToFloat(uint16_t value)
{
	const uint16_t sign = value >> 15;
	const uint16_t exponent = (value >> 10) & 0x1F;
	const uint16_t mantissa = value & 0x03FF;

	float result = 0.0f;
	if (exponent == 0) {
		if (mantissa != 0) {
			result = std::ldexp(static_cast<float>(mantissa) / 1024.0f, -14);
		}
	} else if (exponent == 0x1F) {
		result = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
	} else {
		result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
	}

	return sign ? -result : result;
}

bool IsHdrFormat(enum gs_color_format format)
{
	return format == GS_RGBA16F;
}

bool IsBgraFormat(enum gs_color_format format)
{
	switch (format) {
	case GS_BGRA:
	case GS_BGRX:
	case GS_BGRA_UNORM:
	case GS_BGRX_UNORM:
		return true;
	default:
		return false;
	}
}

bool IsRgbaFormat(enum gs_color_format format)
{
	switch (format) {
	case GS_RGBA:
	case GS_RGBA_UNORM:
		return true;
	default:
		return false;
	}
}

std::string GetShaderPath()
{
	const char *path = obs_module_file("shaders/smart-gamma.effect");
	return path ? path : std::string{};
}

void DestroyDownsampleSurfaces(SmartGammaFilter *filter)
{
	if (!filter)
		return;

	if (filter->downsample_render) {
		gs_texrender_destroy(filter->downsample_render);
		filter->downsample_render = nullptr;
	}

	if (filter->downsample_stage) {
		gs_stagesurface_destroy(filter->downsample_stage);
		filter->downsample_stage = nullptr;
	}
}

bool EnsureDownsampleSurfaces(SmartGammaFilter *filter, enum gs_color_format format)
{
	if (!filter)
		return false;

	if (filter->downsample_render && filter->downsample_stage && filter->downsample_format == format)
		return true;

	DestroyDownsampleSurfaces(filter);

	filter->downsample_render = gs_texrender_create(format, GS_ZS_NONE);
	filter->downsample_stage =
		gs_stagesurface_create(filter->downsample_size, filter->downsample_size, gs_generalize_format(format));
	if (filter->downsample_stage)
		filter->downsample_format = gs_stagesurface_get_color_format(filter->downsample_stage);
	else
		filter->downsample_format = GS_RGBA;
	if (!filter->downsample_render || !filter->downsample_stage) {
		DestroyDownsampleSurfaces(filter);
		return false;
	}

	return true;
}

void DestroyGraphicsResources(SmartGammaFilter *filter)
{
	if (!filter)
		return;

	obs_enter_graphics();
	if (filter->effect) {
		gs_effect_destroy(filter->effect);
		filter->effect = nullptr;
		filter->strength_param = nullptr;
		filter->gamma_param = nullptr;
		filter->brightness_param = nullptr;
		filter->contrast_param = nullptr;
		filter->saturation_param = nullptr;
	}

	DestroyDownsampleSurfaces(filter);
	obs_leave_graphics();
}

bool CreateGraphicsResources(SmartGammaFilter *filter)
{
	if (!filter)
		return false;

	bool success = true;
	obs_enter_graphics();

	if (!EnsureDownsampleSurfaces(filter, GS_RGBA))
		success = false;

	if (success) {
		const std::string shader_path = GetShaderPath();
		char *errors = nullptr;
		filter->effect = gs_effect_create_from_file(shader_path.c_str(), &errors);
		if (!filter->effect) {
			blog(LOG_ERROR, "Smart Gamma: failed to load shader %s (%s)", shader_path.c_str(),
			     errors ? errors : "unknown");
			success = false;
		} else {
			filter->strength_param = gs_effect_get_param_by_name(filter->effect, "effect_strength");
			filter->gamma_param = gs_effect_get_param_by_name(filter->effect, "gamma_adjust");
			filter->brightness_param = gs_effect_get_param_by_name(filter->effect, "brightness_offset");
			filter->contrast_param = gs_effect_get_param_by_name(filter->effect, "contrast_adjust");
			filter->saturation_param = gs_effect_get_param_by_name(filter->effect, "saturation_adjust");
		}
		if (errors)
			bfree(errors);
	}

	obs_leave_graphics();
	return success;
}

void ResetState(SmartGammaFilter *filter)
{
	if (!filter)
		return;
	filter->state = SmartGammaState::Idle;
	filter->effect_strength = 0.0f;
	filter->smoothed_luminance = 1.0f;
	filter->latest_luminance = 1.0f;
	filter->time_below_threshold = 0.0f;
	filter->time_above_threshold = 0.0f;
	filter->pending_tick_delta = 0.0f;
	filter->luminance_initialized = false;
	filter->time_since_last_sample = 0.0f;
	filter->downsample_format = GS_RGBA;
	filter->displayed_luminance_percent.store(100.0f, std::memory_order_relaxed);
	filter->last_properties_update_percent = -1.0f;
}

void UpdateSettingsFromObs(SmartGammaFilter *filter, obs_data_t *settings)
{
	if (!filter || !settings)
		return;

	for (std::size_t i = 0; i < static_cast<std::size_t>(smart_gamma::Parameter::Count); ++i) {
		const auto &descriptor = smart_gamma::kParameterDescriptors[i];
		const double value = obs_data_get_double(settings, descriptor.settings_key);
		switch (static_cast<smart_gamma::Parameter>(i)) {
		case smart_gamma::Parameter::DarknessThreshold: {
			const bool stored_as_percent = obs_data_get_bool(settings, kDarknessThresholdPercentKey);
			const float stored_value = static_cast<float>(value);
			if (stored_as_percent || stored_value > 1.0f) {
				filter->settings.darkness_threshold = stored_value / 100.0f;
				obs_data_set_bool(settings, kDarknessThresholdPercentKey, true);
			} else {
				// Older builds stored the normalized 0-1 value directly; preserve that scale and migrate once.
				filter->settings.darkness_threshold = clamp01(stored_value);
				obs_data_set_double(settings, descriptor.settings_key,
						    static_cast<double>(filter->settings.darkness_threshold * 100.0f));
				obs_data_set_bool(settings, kDarknessThresholdPercentKey, true);
			}
			break;
		}
		case smart_gamma::Parameter::ThresholdDurationMs:
			filter->settings.threshold_duration_ms = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::FadeInMs:
			filter->settings.fade_in_ms = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::FadeOutMs:
			filter->settings.fade_out_ms = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::Gamma:
			filter->settings.gamma = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::Brightness:
			filter->settings.brightness = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::Contrast:
			filter->settings.contrast = static_cast<float>(value);
			break;
		case smart_gamma::Parameter::Saturation:
			filter->settings.saturation = static_cast<float>(value);
			break;
		default:
			break;
		}
	}
}

float SampleLuminance(SmartGammaFilter *filter)
{
	if (!filter || !filter->downsample_render || !filter->downsample_stage || !filter->context)
		return filter ? filter->latest_luminance : 1.0f;

	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);
	if (!target || !parent)
		return filter->latest_luminance;

	const enum gs_color_space preferred_spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	const enum gs_color_space source_space =
		obs_source_get_color_space(target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	const enum gs_color_format required_format = gs_get_format_from_space(source_space);
	if (!EnsureDownsampleSurfaces(filter, required_format))
		return filter->latest_luminance;

	const uint32_t size = filter->downsample_size;
	float luminance = filter->latest_luminance;

	gs_texrender_reset(filter->downsample_render);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin_with_color_space(filter->downsample_render, size, size, source_space)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(size), 0.0f, static_cast<float>(size), -100.0f, 100.0f);

		const uint32_t parent_flags = obs_source_get_output_flags(target);
		const bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		const bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;

		gs_matrix_push();
		gs_matrix_identity();
		const uint32_t source_width = obs_source_get_base_width(target);
		const uint32_t source_height = obs_source_get_base_height(target);
		if (source_width > 0 && source_height > 0) {
			const float scale_x = static_cast<float>(size) / static_cast<float>(source_width);
			const float scale_y = static_cast<float>(size) / static_cast<float>(source_height);
			gs_matrix_scale3f(scale_x, scale_y, 1.0f);
		}

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_matrix_pop();

		gs_texrender_end(filter->downsample_render);
	}

	gs_blend_state_pop();

	gs_texture_t *downsampled = gs_texrender_get_texture(filter->downsample_render);
	if (downsampled) {
		gs_stage_texture(filter->downsample_stage, downsampled);
		gs_flush();
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(filter->downsample_stage, &data, &linesize)) {
			const bool hdr_format = IsHdrFormat(filter->downsample_format);
			const bool bgra_format = IsBgraFormat(filter->downsample_format);
			const bool rgba_format = IsRgbaFormat(filter->downsample_format);
			const uint32_t pixel_stride = hdr_format ? 8u : 4u;
			const double ldr_scale = 1.0 / 255.0;
			double accum = 0.0;
			for (uint32_t y = 0; y < size; ++y) {
				const uint8_t *row = data + (static_cast<size_t>(y) * linesize);
				for (uint32_t x = 0; x < size; ++x) {
					const uint8_t *pixel = row + static_cast<size_t>(x) * pixel_stride;
					float r = 0.0f;
					float g = 0.0f;
					float b = 0.0f;
					if (hdr_format) {
						const auto *channels = reinterpret_cast<const uint16_t *>(pixel);
						r = clamp01(HalfToFloat(channels[0]));
						g = clamp01(HalfToFloat(channels[1]));
						b = clamp01(HalfToFloat(channels[2]));
					} else if (bgra_format) {
						r = static_cast<float>(pixel[2]) * static_cast<float>(ldr_scale);
						g = static_cast<float>(pixel[1]) * static_cast<float>(ldr_scale);
						b = static_cast<float>(pixel[0]) * static_cast<float>(ldr_scale);
					} else if (rgba_format) {
						r = static_cast<float>(pixel[0]) * static_cast<float>(ldr_scale);
						g = static_cast<float>(pixel[1]) * static_cast<float>(ldr_scale);
						b = static_cast<float>(pixel[2]) * static_cast<float>(ldr_scale);
					} else {
						// Fallback: assume first three channels are RGB order
						r = static_cast<float>(pixel[0]) * static_cast<float>(ldr_scale);
						g = static_cast<float>(pixel[1]) * static_cast<float>(ldr_scale);
						b = static_cast<float>(pixel[2]) * static_cast<float>(ldr_scale);
					}
					accum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
				}
			}
			const double count = static_cast<double>(size) * static_cast<double>(size);
			luminance = static_cast<float>(accum / std::max(count, 1.0));
			gs_stagesurface_unmap(filter->downsample_stage);
		}
	}

	filter->latest_luminance = clamp01(luminance);
	if (!filter->luminance_initialized) {
		filter->smoothed_luminance = filter->latest_luminance;
		filter->luminance_initialized = true;
	}
	return filter->latest_luminance;
}

void MaybeUpdateLuminanceDisplay(SmartGammaFilter *filter)
{
	if (!filter || !filter->context)
		return;

	const float percent = clamp01(filter->smoothed_luminance) * 100.0f;
	filter->displayed_luminance_percent.store(percent, std::memory_order_relaxed);

	const bool needs_refresh = filter->last_properties_update_percent < 0.0f ||
				   std::fabs(percent - filter->last_properties_update_percent) >= 0.5f;
	if (!needs_refresh)
		return;

	filter->last_properties_update_percent = percent;
	obs_source_update_properties(filter->context);
}

void UpdateStateMachine(SmartGammaFilter *filter, float delta_seconds, float luminance)
{
	if (!filter)
		return;

	if (delta_seconds <= 0.0f)
		delta_seconds = 1.0f / 60.0f;

	filter->smoothed_luminance = lerp(filter->smoothed_luminance, luminance, clamp01(kLuminanceSmoothing));
	const bool is_dark = filter->smoothed_luminance <= filter->settings.darkness_threshold;

	const float threshold_duration = std::max(filter->settings.threshold_duration_ms / 1000.0f, 0.0f);
	const float fade_in_seconds = std::max(filter->settings.fade_in_ms / 1000.0f, 0.0001f);
	const float fade_out_seconds = std::max(filter->settings.fade_out_ms / 1000.0f, 0.0001f);

	if (is_dark) {
		filter->time_below_threshold += delta_seconds;
		filter->time_above_threshold = 0.0f;
	} else {
		filter->time_above_threshold += delta_seconds;
		filter->time_below_threshold = 0.0f;
	}

	const bool dark_duration_met = threshold_duration <= 0.0f || filter->time_below_threshold >= threshold_duration;
	const bool light_duration_met = threshold_duration <= 0.0f ||
					filter->time_above_threshold >= threshold_duration;

	switch (filter->state) {
	case SmartGammaState::Idle:
		filter->effect_strength = 0.0f;
		if (is_dark) {
			if (dark_duration_met) {
				filter->state = SmartGammaState::FadingIn;
			} else {
				filter->state = SmartGammaState::WaitingForThreshold;
			}
		}
		break;

	case SmartGammaState::WaitingForThreshold:
		if (!is_dark) {
			filter->state = SmartGammaState::Idle;
			filter->time_below_threshold = 0.0f;
		} else if (dark_duration_met) {
			filter->state = SmartGammaState::FadingIn;
		}
		break;

	case SmartGammaState::FadingIn:
		if (!is_dark) {
			if (light_duration_met) {
				filter->state = SmartGammaState::FadingOut;
				break;
			}
		}
		if (is_dark) {
			filter->effect_strength = clamp01(filter->effect_strength + (delta_seconds / fade_in_seconds));
			if (filter->effect_strength >= 1.0f - kEpsilon) {
				filter->effect_strength = 1.0f;
				filter->state = SmartGammaState::Active;
			}
		}
		break;

	case SmartGammaState::Active:
		filter->effect_strength = 1.0f;
		if (!is_dark && light_duration_met)
			filter->state = SmartGammaState::FadingOut;
		break;

	case SmartGammaState::FadingOut:
		if (is_dark && dark_duration_met) {
			filter->state = SmartGammaState::FadingIn;
			break;
		}
		filter->effect_strength = clamp01(filter->effect_strength - (delta_seconds / fade_out_seconds));
		if (filter->effect_strength <= kEpsilon) {
			filter->effect_strength = 0.0f;
			filter->state = SmartGammaState::Idle;
		}
		break;
	}

	MaybeUpdateLuminanceDisplay(filter);
}

void UploadShaderParams(SmartGammaFilter *filter)
{
	if (!filter || !filter->effect)
		return;

	if (filter->strength_param)
		gs_effect_set_float(filter->strength_param, clamp01(filter->effect_strength));
	if (filter->gamma_param)
		gs_effect_set_float(filter->gamma_param, std::max(filter->settings.gamma, 0.01f));
	if (filter->brightness_param)
		gs_effect_set_float(filter->brightness_param, filter->settings.brightness);
	if (filter->contrast_param)
		gs_effect_set_float(filter->contrast_param, filter->settings.contrast);
	if (filter->saturation_param)
		gs_effect_set_float(filter->saturation_param, filter->settings.saturation);
}

const char *SmartGammaGetName(void * /*unused*/)
{
	return obs_module_text("SmartGamma.FilterName");
}

void *SmartGammaCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new SmartGammaFilter();
	filter->context = source;
	filter->downsample_size = kDefaultDownsampleSize;
	ResetState(filter);

	if (!CreateGraphicsResources(filter)) {
		DestroyGraphicsResources(filter);
		delete filter;
		return nullptr;
	}

	UpdateSettingsFromObs(filter, settings);
	return filter;
}

void SmartGammaDestroy(void *data)
{
	auto *filter = static_cast<SmartGammaFilter *>(data);
	DestroyGraphicsResources(filter);
	delete filter;
}

void SmartGammaUpdate(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<SmartGammaFilter *>(data);
	UpdateSettingsFromObs(filter, settings);
	if (filter) {
		filter->luminance_initialized = false;
		filter->pending_tick_delta = 0.0f;
		filter->time_since_last_sample = 0.0f;
	}
}

void SmartGammaTick(void *data, float seconds)
{
	auto *filter = static_cast<SmartGammaFilter *>(data);
	if (!filter)
		return;
	filter->pending_tick_delta += seconds;
}

void SmartGammaRender(void *data, gs_effect_t * /*effect*/)
{
	auto *filter = static_cast<SmartGammaFilter *>(data);
	if (!filter || !filter->effect) {
		if (filter && filter->context)
			obs_source_skip_video_filter(filter->context);
		return;
	}

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	float delta = filter->pending_tick_delta;
	if (delta <= 0.0f)
		delta = 1.0f / 60.0f;
	filter->pending_tick_delta = 0.0f;
	filter->time_since_last_sample += delta;

	const bool should_sample_luminance = !filter->luminance_initialized ||
					     filter->time_since_last_sample >= kLuminanceSampleIntervalSeconds;
	const float luminance = should_sample_luminance ? SampleLuminance(filter) : filter->latest_luminance;
	if (should_sample_luminance)
		filter->time_since_last_sample = 0.0f;

	UpdateStateMachine(filter, delta, luminance);
	UploadShaderParams(filter);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);
}

obs_properties_t *SmartGammaProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	auto *filter = static_cast<SmartGammaFilter *>(data);

	const char *usage_title = obs_module_text("SmartGamma.UsageTitle");
	const char *usage_text = obs_module_text("SmartGamma.UsageText");
	obs_property_t *usage_prop = obs_properties_add_text(props, "smart_gamma_usage", usage_title, OBS_TEXT_INFO);
	if (usage_prop) {
		obs_property_set_long_description(usage_prop, usage_text);
		obs_property_text_set_info_word_wrap(usage_prop, true);
		obs_property_set_enabled(usage_prop, false);
	}

	const char *current_label = obs_module_text("SmartGamma.Param.CurrentLuminance");
	obs_property_t *current_luminance_prop =
		obs_properties_add_text(props, "smart_gamma_current_luminance", current_label, OBS_TEXT_INFO);
	if (current_luminance_prop) {
		const float percent = filter ? filter->displayed_luminance_percent.load(std::memory_order_relaxed)
					     : 0.0f;
		const char *format = obs_module_text("SmartGamma.Param.CurrentLuminance.Value");
		if (!format || format[0] == '\0')
			format = "Detected brightness: %.1f%%";
		char buffer[96];
		std::snprintf(buffer, sizeof(buffer), format, percent);
		obs_property_set_long_description(current_luminance_prop, buffer);
		obs_property_set_enabled(current_luminance_prop, false);
		obs_property_text_set_info_word_wrap(current_luminance_prop, true);
	}

	for (const auto &descriptor : smart_gamma::kParameterDescriptors) {
		const char *label = obs_module_text(descriptor.label_token);
		obs_property_t *prop = obs_properties_add_float_slider(props, descriptor.settings_key, label,
								       descriptor.min_value, descriptor.max_value,
								       descriptor.step);
		if (prop) {
			const char *description = obs_module_text(descriptor.description_token);
			obs_property_set_long_description(prop, description);

			const std::string description_id = std::string(descriptor.settings_key) + "_description";
			obs_property_t *description_prop =
				obs_properties_add_text(props, description_id.c_str(), description, OBS_TEXT_INFO);
			if (description_prop)
				obs_property_set_enabled(description_prop, false);
		}
	}

	const std::string plugin_name = obs_module_text("SmartGamma.FilterName");
	const std::string plugin_info = "<a href=\"" + std::string(SMART_GAMMA_REPO) + "\">" + plugin_name + "</a> v" +
					std::string(SMART_GAMMA_VERSION) + " by <a href=\"" + kAuthorWebsite + "\">" +
					std::string(SMART_GAMMA_AUTHOR) + "</a>";
	obs_property_t *plugin_info_prop =
		obs_properties_add_text(props, "smart_gamma_plugin_info", plugin_info.c_str(), OBS_TEXT_INFO);
	if (plugin_info_prop)
		obs_property_set_enabled(plugin_info_prop, false);

	return props;
}

void SmartGammaDefaults(obs_data_t *settings)
{
	for (const auto &descriptor : smart_gamma::kParameterDescriptors) {
		obs_data_set_default_double(settings, descriptor.settings_key, descriptor.default_value);
	}
	obs_data_set_default_bool(settings, kDarknessThresholdPercentKey, true);
}

obs_source_info BuildSourceInfo()
{
	obs_source_info info = {};
	info.id = "smart_gamma_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = SmartGammaGetName;
	info.create = SmartGammaCreate;
	info.destroy = SmartGammaDestroy;
	info.get_defaults = SmartGammaDefaults;
	info.get_properties = SmartGammaProperties;
	info.update = SmartGammaUpdate;
	info.video_render = SmartGammaRender;
	info.video_tick = SmartGammaTick;
	return info;
}

obs_source_info SmartGammaFilterInfo = BuildSourceInfo();

} // namespace

const char *obs_module_description(void)
{
	return obs_module_text("SmartGamma.ModuleDescription");
}

bool obs_module_load(void)
{
	obs_register_source(&SmartGammaFilterInfo);
	blog(LOG_INFO, "Smart Gamma filter registered");
	return true;
}

void obs_module_unload(void) {}
