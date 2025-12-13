#pragma once

#include <array>
#include <cstddef>

namespace smart_gamma {

enum class Parameter : std::size_t {
	DarknessThreshold = 0,
	ThresholdDurationMs,
	FadeInMs,
	FadeOutMs,
	Gamma,
	Brightness,
	Contrast,
	Saturation,
	Count
};

struct ParameterDescriptor {
	const char *settings_key;
	const char *label_token;
	const char *description_token;
	double min_value;
	double max_value;
	double step;
	double default_value;
};

inline constexpr std::array<ParameterDescriptor, static_cast<std::size_t>(Parameter::Count)> kParameterDescriptors{{
	{"darkness_threshold", "SmartGamma.Param.DarknessThreshold", "SmartGamma.Param.DarknessThreshold.Description",
	 0.0, 100.0, 1.0, 35.0},
	{"activation_delay_ms", "SmartGamma.Param.ThresholdDuration", "SmartGamma.Param.ThresholdDuration.Description",
	 0.0, 20000.0, 10.0, 600.0},
	{"fade_in_ms", "SmartGamma.Param.FadeIn", "SmartGamma.Param.FadeIn.Description", 0.0, 20000.0, 10.0, 200.0},
	{"fade_out_ms", "SmartGamma.Param.FadeOut", "SmartGamma.Param.FadeOut.Description", 0.0, 20000.0, 10.0, 450.0},
	{"gamma", "SmartGamma.Param.Gamma", "SmartGamma.Param.Gamma.Description", 0.5, 3.0, 0.01, 1.20},
	{"brightness", "SmartGamma.Param.Brightness", "SmartGamma.Param.Brightness.Description", -0.5, 0.5, 0.01, 0.10},
	{"contrast", "SmartGamma.Param.Contrast", "SmartGamma.Param.Contrast.Description", 0.5, 2.0, 0.01, 1.10},
	{"saturation", "SmartGamma.Param.Saturation", "SmartGamma.Param.Saturation.Description", 0.0, 2.5, 0.01, 1.00},
}};

inline constexpr const ParameterDescriptor &GetDescriptor(Parameter parameter) noexcept
{
	return kParameterDescriptors[static_cast<std::size_t>(parameter)];
}

inline constexpr double DefaultValue(Parameter parameter) noexcept
{
	return GetDescriptor(parameter).default_value;
}

} // namespace smart_gamma
