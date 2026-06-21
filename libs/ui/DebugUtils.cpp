/*
 * Copyright 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ui/DebugUtils.h>
#include <ui/DeviceProductInfo.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>

#include <android-base/stringprintf.h>
#include <hardware/gralloc.h>
#include <string>

using android::base::StringPrintf;
using android::ui::ColorMode;
using android::ui::RenderIntent;

std::string decodeStandardOnly(uint32_t dataspaceStandard) {
    switch (dataspaceStandard) {
        case HAL_DATASPACE_STANDARD_BT709:
            return std::string("BT709");

        case HAL_DATASPACE_STANDARD_BT601_625:
            return std::string("BT601_625");

        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
            return std::string("BT601_625_UNADJUSTED");

        case HAL_DATASPACE_STANDARD_BT601_525:
            return std::string("BT601_525");

        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
            return std::string("BT601_525_UNADJUSTED");

        case HAL_DATASPACE_STANDARD_BT2020:
            return std::string("BT2020");

        case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
            return std::string("BT2020 (constant luminance)");

        case HAL_DATASPACE_STANDARD_BT470M:
            return std::string("BT470M");

        case HAL_DATASPACE_STANDARD_FILM:
            return std::string("FILM");

        case HAL_DATASPACE_STANDARD_DCI_P3:
            return std::string("DCI-P3");

        case HAL_DATASPACE_STANDARD_ADOBE_RGB:
            return std::string("AdobeRGB");
    }

    return StringPrintf("Unknown dataspace code %d", dataspaceStandard);
}

std::string decodeStandard(android_dataspace dataspace) {
    const uint32_t dataspaceStandard = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    if (dataspaceStandard == 0) {
        switch (dataspace & 0xffff) {
            case HAL_DATASPACE_JFIF:
                return std::string("(deprecated) JFIF (BT601_625)");

            case HAL_DATASPACE_BT601_625:
                return std::string("(deprecated) BT601_625");

            case HAL_DATASPACE_BT601_525:
                return std::string("(deprecated) BT601_525");

            case HAL_DATASPACE_SRGB_LINEAR:
            case HAL_DATASPACE_SRGB:
                return std::string("(deprecated) sRGB");

            case HAL_DATASPACE_BT709:
                return std::string("(deprecated) BT709");

            case HAL_DATASPACE_ARBITRARY:
                return std::string("ARBITRARY");

            case HAL_DATASPACE_UNKNOWN:
            // Fallthrough
            default:
                return StringPrintf("Unknown deprecated dataspace code %d", dataspace);
        }
    }
    return decodeStandardOnly(dataspaceStandard);
}

std::string decodeTransferOnly(uint32_t dataspaceTransfer) {
    switch (dataspaceTransfer) {
        case HAL_DATASPACE_TRANSFER_UNSPECIFIED:
            return std::string("Unspecified");

        case HAL_DATASPACE_TRANSFER_LINEAR:
            return std::string("Linear");

        case HAL_DATASPACE_TRANSFER_SRGB:
            return std::string("sRGB");

        case HAL_DATASPACE_TRANSFER_SMPTE_170M:
            return std::string("SMPTE_170M");

        case HAL_DATASPACE_TRANSFER_GAMMA2_2:
            return std::string("gamma 2.2");

        case HAL_DATASPACE_TRANSFER_GAMMA2_6:
            return std::string("gamma 2.6");

        case HAL_DATASPACE_TRANSFER_GAMMA2_8:
            return std::string("gamma 2.8");

        case HAL_DATASPACE_TRANSFER_ST2084:
            return std::string("SMPTE 2084");

        case HAL_DATASPACE_TRANSFER_HLG:
            return std::string("STD-B67");
    }

    return StringPrintf("Unknown dataspace transfer %d", dataspaceTransfer);
}

std::string decodeTransfer(android_dataspace dataspace) {
    const uint32_t dataspaceSelect = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    if (dataspaceSelect == 0) {
        switch (dataspace & 0xffff) {
            case HAL_DATASPACE_JFIF:
            case HAL_DATASPACE_BT601_625:
            case HAL_DATASPACE_BT601_525:
            case HAL_DATASPACE_BT709:
                return std::string("SMPTE_170M");

            case HAL_DATASPACE_SRGB_LINEAR:
            case HAL_DATASPACE_ARBITRARY:
                return std::string("Linear");

            case HAL_DATASPACE_SRGB:
                return std::string("sRGB");

            case HAL_DATASPACE_UNKNOWN:
            // Fallthrough
            default:
                return std::string("");
        }
    }

    const uint32_t dataspaceTransfer = (dataspace & HAL_DATASPACE_TRANSFER_MASK);
    return decodeTransferOnly(dataspaceTransfer);
}

std::string decodeRangeOnly(uint32_t dataspaceRange) {
    switch (dataspaceRange) {
        case HAL_DATASPACE_RANGE_UNSPECIFIED:
            return std::string("Range Unspecified");

        case HAL_DATASPACE_RANGE_FULL:
            return std::string("Full range");

        case HAL_DATASPACE_RANGE_LIMITED:
            return std::string("Limited range");

        case HAL_DATASPACE_RANGE_EXTENDED:
            return std::string("Extended range");
    }

    return StringPrintf("Unknown dataspace range %d", dataspaceRange);
}

std::string decodeRange(android_dataspace dataspace) {
    const uint32_t dataspaceSelect = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    if (dataspaceSelect == 0) {
        switch (dataspace & 0xffff) {
            case HAL_DATASPACE_JFIF:
            case HAL_DATASPACE_SRGB_LINEAR:
            case HAL_DATASPACE_SRGB:
                return std::string("Full range");

            case HAL_DATASPACE_BT601_625:
            case HAL_DATASPACE_BT601_525:
            case HAL_DATASPACE_BT709:
                return std::string("Limited range");

            case HAL_DATASPACE_ARBITRARY:
            case HAL_DATASPACE_UNKNOWN:
            // Fallthrough
            default:
                return std::string("unspecified range");
        }
    }

    const uint32_t dataspaceRange = (dataspace & HAL_DATASPACE_RANGE_MASK);
    return decodeRangeOnly(dataspaceRange);
}

std::string dataspaceDetails(android_dataspace dataspace) {
    if (dataspace == 0) {
        return "Default";
    }
    return StringPrintf("%s %s %s", decodeStandard(dataspace).c_str(),
                        decodeTransfer(dataspace).c_str(), decodeRange(dataspace).c_str());
}

std::string decodeColorMode(ColorMode colorMode) {
    switch (colorMode) {
        case ColorMode::NATIVE:
            return std::string("ColorMode::NATIVE");

        case ColorMode::STANDARD_BT601_625:
            return std::string("ColorMode::BT601_625");

        case ColorMode::STANDARD_BT601_625_UNADJUSTED:
            return std::string("ColorMode::BT601_625_UNADJUSTED");

        case ColorMode::STANDARD_BT601_525:
            return std::string("ColorMode::BT601_525");

        case ColorMode::STANDARD_BT601_525_UNADJUSTED:
            return std::string("ColorMode::BT601_525_UNADJUSTED");

        case ColorMode::STANDARD_BT709:
            return std::string("ColorMode::BT709");

        case ColorMode::DCI_P3:
            return std::string("ColorMode::DCI_P3");

        case ColorMode::SRGB:
            return std::string("ColorMode::SRGB");

        case ColorMode::ADOBE_RGB:
            return std::string("ColorMode::ADOBE_RGB");

        case ColorMode::DISPLAY_P3:
            return std::string("ColorMode::DISPLAY_P3");

        case ColorMode::BT2020:
            return std::string("ColorMode::BT2020");

        case ColorMode::DISPLAY_BT2020:
            return std::string("ColorMode::DISPLAY_BT2020");

        case ColorMode::BT2100_PQ:
            return std::string("ColorMode::BT2100_PQ");

        case ColorMode::BT2100_HLG:
            return std::string("ColorMode::BT2100_HLG");
    }

    return StringPrintf("Unknown color mode %d", colorMode);
}

std::string decodeColorTransform(android_color_transform colorTransform) {
    switch (colorTransform) {
        case HAL_COLOR_TRANSFORM_IDENTITY:
            return std::string("Identity");

        case HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX:
            return std::string("Arbitrary matrix");

        case HAL_COLOR_TRANSFORM_VALUE_INVERSE:
            return std::string("Inverse value");

        case HAL_COLOR_TRANSFORM_GRAYSCALE:
            return std::string("Grayscale");

        case HAL_COLOR_TRANSFORM_CORRECT_PROTANOPIA:
            return std::string("Correct protanopia");

        case HAL_COLOR_TRANSFORM_CORRECT_DEUTERANOPIA:
            return std::string("Correct deuteranopia");

        case HAL_COLOR_TRANSFORM_CORRECT_TRITANOPIA:
            return std::string("Correct tritanopia");
    }

    return StringPrintf("Unknown color transform %d", colorTransform);
}

// Converts a PixelFormat to a human-readable string.
// (Could use a table of prefab String8 objects.)
std::string decodePixelFormat(android::PixelFormat format) {
    switch (format) {
        case android::PIXEL_FORMAT_UNKNOWN:
            return std::string("Unknown/None");
        case android::PIXEL_FORMAT_CUSTOM:
            return std::string("Custom");
        case android::PIXEL_FORMAT_TRANSLUCENT:
            return std::string("Translucent");
        case android::PIXEL_FORMAT_TRANSPARENT:
            return std::string("Transparent");
        case android::PIXEL_FORMAT_OPAQUE:
            return std::string("Opaque");
        case android::PIXEL_FORMAT_RGBA_8888:
            return std::string("RGBA_8888");
        case android::PIXEL_FORMAT_RGBX_8888:
            return std::string("RGBx_8888");
        case android::PIXEL_FORMAT_RGBA_FP16:
            return std::string("RGBA_FP16");
        case android::PIXEL_FORMAT_RGBA_1010102:
            return std::string("RGBA_1010102");
        case android::PIXEL_FORMAT_RGB_888:
            return std::string("RGB_888");
        case android::PIXEL_FORMAT_RGB_565:
            return std::string("RGB_565");
        case android::PIXEL_FORMAT_BGRA_8888:
            return std::string("BGRA_8888");
        case android::PIXEL_FORMAT_R_8:
            return std::string("R_8");
        case android::PIXEL_FORMAT_R_16_UINT:
            return std::string("R_16_UINT");
        case android::PIXEL_FORMAT_RG_1616_UINT:
            return std::string("RG_1616_UINT");
        case android::PIXEL_FORMAT_RGBA_10101010:
            return std::string("RGBA_10101010");
        case android::PIXEL_FORMAT_BGRA_1010102:
            return std::string("BGRA_1010102");
        case android::PIXEL_FORMAT_BGRX_1010102:
            return std::string("BGRX_1010102");
        default:
            return StringPrintf("Unknown %#08x", format);
    }
}

std::string decodeRenderIntent(RenderIntent renderIntent) {
    switch(renderIntent) {
      case RenderIntent::COLORIMETRIC:
          return std::string("RenderIntent::COLORIMETRIC");
      case RenderIntent::ENHANCE:
          return std::string("RenderIntent::ENHANCE");
      case RenderIntent::TONE_MAP_COLORIMETRIC:
          return std::string("RenderIntent::TONE_MAP_COLORIMETRIC");
      case RenderIntent::TONE_MAP_ENHANCE:
          return std::string("RenderIntent::TONE_MAP_ENHANCE");
    }
    return std::string("Unknown RenderIntent");
}

#define DECODE_GRALLOC_USAGE_CASE(SS, USAGE, FLAG)                \
    if ((USAGE & GRALLOC_USAGE_##FLAG) == GRALLOC_USAGE_##FLAG) { \
        USAGE &= (~static_cast<uint64_t>(GRALLOC_USAGE_##FLAG));  \
        SS << #FLAG << " | ";                                     \
    }

std::string decodeGrallocUsage(uint64_t usage) {
    if (!usage) {
        return "NONE";
    }
    std::stringstream ss;
    // Flags are incrementally cleared from `usage` as strings are appended to `ss`.
    // Note: _OFTEN flags must be checked / cleared before their associated _RARELY flags, because
    // _OFTEN flags are a superset of multiple overlapping bits (e.g. 0b11 vs. 0b10). _HW_CAMERA_ZSL
    // also shares this property, w.r.t. _HW_CAMERA_READ.
    DECODE_GRALLOC_USAGE_CASE(ss, usage, SW_READ_OFTEN); // Must be checked before SW_READ_RARELY
    DECODE_GRALLOC_USAGE_CASE(ss, usage, SW_READ_RARELY);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, SW_WRITE_OFTEN); // Must be checked before SW_WRITE_RARELY
    DECODE_GRALLOC_USAGE_CASE(ss, usage, SW_WRITE_RARELY);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_TEXTURE);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_RENDER);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_2D);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_COMPOSER);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_FB);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, EXTERNAL_DISP);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, PROTECTED);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, CURSOR);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_VIDEO_ENCODER);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_CAMERA_WRITE);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_CAMERA_ZSL); // Must be checked before HW_CAMERA_READ
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_CAMERA_READ);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, RENDERSCRIPT);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, FOREIGN_BUFFERS);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, HW_IMAGE_ENCODER);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, PRIVATE_0);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, PRIVATE_1);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, PRIVATE_2);
    DECODE_GRALLOC_USAGE_CASE(ss, usage, PRIVATE_3);
    // Append any remaining unknown usage bits.
    if (usage) {
        ss << "0x" << std::hex << std::uppercase << usage << " | ";
    }
    auto result = ss.str();
    result.erase(result.size() - 3, 3); // Remove trailing separator.
    return result;
}

std::string toString(const android::DeviceProductInfo::ManufactureOrModelDate& date) {
    using ModelYear = android::DeviceProductInfo::ModelYear;
    using ManufactureYear = android::DeviceProductInfo::ManufactureYear;
    using ManufactureWeekAndYear = android::DeviceProductInfo::ManufactureWeekAndYear;

    if (const auto* model = std::get_if<ModelYear>(&date)) {
        return StringPrintf("ModelYear{%d}", model->year);
    } else if (const auto* manufacture = std::get_if<ManufactureYear>(&date)) {
        return StringPrintf("ManufactureDate{year=%d}", manufacture->year);
    } else if (const auto* manufacture = std::get_if<ManufactureWeekAndYear>(&date)) {
        return StringPrintf("ManufactureDate{week=%d, year=%d}", manufacture->week,
                            manufacture->year);
    } else {
        LOG_FATAL("Unknown alternative for variant DeviceProductInfo::ManufactureOrModelDate");
        return {};
    }
}

std::string toString(const android::DeviceProductInfo& info) {
    return StringPrintf("DeviceProductInfo{name=%s, productId=%s, manufacturerPnpId=%s, "
                        "manufactureOrModelDate=%s}",
                        info.name.data(), info.productId.data(), info.manufacturerPnpId.data(),
                        toString(info.manufactureOrModelDate).c_str());
}
