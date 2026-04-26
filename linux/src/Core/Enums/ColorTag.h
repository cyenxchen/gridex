#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gridex {

enum class ColorTag {
    Red,
    Orange,
    Green,
    Blue,
    Purple,
    Gray,
};

inline constexpr std::array<ColorTag, 6> kAllColorTags = {
    ColorTag::Red,    ColorTag::Orange, ColorTag::Green,
    ColorTag::Blue,   ColorTag::Purple, ColorTag::Gray,
};

struct RgbColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

inline std::string_view rawValue(ColorTag c) {
    switch (c) {
        case ColorTag::Red:    return "red";
        case ColorTag::Orange: return "orange";
        case ColorTag::Green:  return "green";
        case ColorTag::Blue:   return "blue";
        case ColorTag::Purple: return "purple";
        case ColorTag::Gray:   return "gray";
    }
    return "";
}

inline std::optional<ColorTag> colorTagFromRaw(std::string_view raw) {
    if (raw == "red")    return ColorTag::Red;
    if (raw == "orange") return ColorTag::Orange;
    if (raw == "green")  return ColorTag::Green;
    if (raw == "blue")   return ColorTag::Blue;
    if (raw == "purple") return ColorTag::Purple;
    if (raw == "gray")   return ColorTag::Gray;
    return std::nullopt;
}

inline constexpr RgbColor rgbColor(ColorTag c) {
    switch (c) {
        case ColorTag::Red:    return {226,  75,  74};
        case ColorTag::Orange: return {239, 159,  39};
        case ColorTag::Green:  return { 99, 153,  34};
        case ColorTag::Blue:   return { 55, 138, 221};
        case ColorTag::Purple: return { 83,  74, 183};
        case ColorTag::Gray:   return {128, 128, 128};
    }
    return {};
}

inline std::string_view environmentHint(ColorTag c) {
    switch (c) {
        case ColorTag::Red:    return "Production";
        case ColorTag::Orange: return "Staging";
        case ColorTag::Green:  return "Development";
        case ColorTag::Blue:   return "Local";
        case ColorTag::Purple: return "Custom";
        case ColorTag::Gray:   return "Other";
    }
    return "";
}

}
