/*
 * Copyright 2022 The Android Open Source Project
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

#include <cmath>

#include <jpegrecoverymap/recoverymapmath.h>

namespace android::recoverymap {

////////////////////////////////////////////////////////////////////////////////
// sRGB transformations

static const float kSrgbR = 0.299f, kSrgbG = 0.587f, kSrgbB = 0.114f;

float srgbLuminance(Color e) {
  return kSrgbR * e.r + kSrgbG * e.g + kSrgbB * e.b;
}

static const float kSrgbRCr = 1.402f, kSrgbGCb = 0.34414f, kSrgbGCr = 0.71414f, kSrgbBCb = 1.772f;

Color srgbYuvToRgb(Color e_gamma) {
  return {{{ e_gamma.y + kSrgbRCr * e_gamma.v,
             e_gamma.y - kSrgbGCb * e_gamma.u - kSrgbGCr * e_gamma.v,
             e_gamma.y + kSrgbBCb * e_gamma.u }}};
}

static const float kSrgbUR = -0.1687f, kSrgbUG = -0.3313f, kSrgbUB = 0.5f;
static const float kSrgbVR = 0.5f, kSrgbVG = -0.4187f, kSrgbVB = -0.0813f;

Color srgbRgbToYuv(Color e_gamma) {
  return {{{ kSrgbR * e_gamma.r + kSrgbG * e_gamma.g + kSrgbB * e_gamma.b,
             kSrgbUR * e_gamma.r + kSrgbUG * e_gamma.g + kSrgbUB * e_gamma.b,
             kSrgbVR * e_gamma.r + kSrgbVG * e_gamma.g + kSrgbVB * e_gamma.b }}};
}

float srgbInvOetf(float e_gamma) {
  if (e_gamma <= 0.04045f) {
    return e_gamma / 12.92f;
  } else {
    return pow((e_gamma + 0.055f) / 1.055f, 2.4);
  }
}

Color srgbInvOetf(Color e_gamma) {
  return {{{ srgbInvOetf(e_gamma.r),
             srgbInvOetf(e_gamma.g),
             srgbInvOetf(e_gamma.b) }}};
}


////////////////////////////////////////////////////////////////////////////////
// Display-P3 transformations

static const float kP3R = 0.22897f, kP3G = 0.69174f, kP3B = 0.07929f;

float p3Luminance(Color e) {
  return kP3R * e.r + kP3G * e.g + kP3B * e.b;
}


////////////////////////////////////////////////////////////////////////////////
// BT.2100 transformations - according to ITU-R BT.2100-2

static const float kBt2100R = 0.2627f, kBt2100G = 0.6780f, kBt2100B = 0.0593f;

float bt2100Luminance(Color e) {
  return kBt2100R * e.r + kBt2100G * e.g + kBt2100B * e.b;
}

static const float kBt2100Cb = 1.8814f, kBt2100Cr = 1.4746f;

Color bt2100RgbToYuv(Color e_gamma) {
  float y_gamma = bt2100Luminance(e_gamma);
  return {{{ y_gamma,
             (e_gamma.b - y_gamma) / kBt2100Cb,
             (e_gamma.r - y_gamma) / kBt2100Cr }}};
}

// Derived from the reverse of bt2100RgbToYuv. The derivation for R and B are
// pretty straight forward; we just reverse the formulas for U and V above. But
// deriving the formula for G is a bit more complicated:
//
// Start with equation for luminance:
//   Y = kBt2100R * R + kBt2100G * G + kBt2100B * B
// Solve for G:
//   G = (Y - kBt2100R * R - kBt2100B * B) / kBt2100B
// Substitute equations for R and B in terms YUV:
//   G = (Y - kBt2100R * (Y + kBt2100Cr * V) - kBt2100B * (Y + kBt2100Cb * U)) / kBt2100B
// Simplify:
//   G = Y * ((1 - kBt2100R - kBt2100B) / kBt2100G)
//     + U * (kBt2100B * kBt2100Cb / kBt2100G)
//     + V * (kBt2100R * kBt2100Cr / kBt2100G)
//
// We then get the following coeficients for calculating G from YUV:
//
// Coef for Y = (1 - kBt2100R - kBt2100B) / kBt2100G = 1
// Coef for U = kBt2100B * kBt2100Cb / kBt2100G = kBt2100GCb = ~0.1645
// Coef for V = kBt2100R * kBt2100Cr / kBt2100G = kBt2100GCr = ~0.5713

static const float kBt2100GCb = kBt2100B * kBt2100Cb / kBt2100G;
static const float kBt2100GCr = kBt2100R * kBt2100Cr / kBt2100G;

Color bt2100YuvToRgb(Color e_gamma) {
  return {{{ e_gamma.y + kBt2100Cr * e_gamma.v,
             e_gamma.y - kBt2100GCb * e_gamma.u - kBt2100GCr * e_gamma.v,
             e_gamma.y + kBt2100Cb * e_gamma.u }}};
}

static const float kHlgA = 0.17883277f, kHlgB = 0.28466892f, kHlgC = 0.55991073;

static float hlgOetf(float e) {
  if (e <= 1.0f/12.0f) {
    return sqrt(3.0f * e);
  } else {
    return kHlgA * log(12.0f * e - kHlgB) + kHlgC;
  }
}

Color hlgOetf(Color e) {
  return {{{ hlgOetf(e.r), hlgOetf(e.g), hlgOetf(e.b) }}};
}

static float hlgInvOetf(float e_gamma) {
  if (e_gamma <= 0.5f) {
    return pow(e_gamma, 2.0f) / 3.0f;
  } else {
    return (exp((e_gamma - kHlgC) / kHlgA) + kHlgB) / 12.0f;
  }
}

Color hlgInvOetf(Color e_gamma) {
  return {{{ hlgInvOetf(e_gamma.r),
             hlgInvOetf(e_gamma.g),
             hlgInvOetf(e_gamma.b) }}};
}

static const float kPqM1 = 2610.0f / 16384.0f, kPqM2 = 2523.0f / 4096.0f * 128.0f;
static const float kPqC1 = 3424.0f / 4096.0f, kPqC2 = 2413.0f / 4096.0f * 32.0f,
                   kPqC3 = 2392.0f / 4096.0f * 32.0f;

static float pqOetf(float e) {
  if (e < 0.0f) e = 0.0f;
  return pow((kPqC1 + kPqC2 * pow(e / 10000.0f, kPqM1)) / (1 + kPqC3 * pow(e / 10000.0f, kPqM1)),
             kPqM2);
}

Color pqOetf(Color e) {
  return {{{ pqOetf(e.r), pqOetf(e.g), pqOetf(e.b) }}};
}

static float pqInvOetf(float e_gamma) {
  static const float kPqInvOetfCoef = log2(-(pow(kPqM1, 1.0f / kPqM2) - kPqC1)
                                         / (kPqC3 * pow(kPqM1, 1.0f / kPqM2) - kPqC2));
  return kPqInvOetfCoef / log2(e_gamma * 10000.0f);
}

Color pqInvOetf(Color e_gamma) {
  return {{{ pqInvOetf(e_gamma.r),
             pqInvOetf(e_gamma.g),
             pqInvOetf(e_gamma.b) }}};
}


////////////////////////////////////////////////////////////////////////////////
// Color conversions

Color bt709ToP3(Color e) {
 return {{{ 0.82254f * e.r + 0.17755f * e.g + 0.00006f * e.b,
            0.03312f * e.r + 0.96684f * e.g + -0.00001f * e.b,
            0.01706f * e.r + 0.07240f * e.g + 0.91049f * e.b }}};
}

Color bt709ToBt2100(Color e) {
 return {{{ 0.62740f * e.r + 0.32930f * e.g + 0.04332f * e.b,
            0.06904f * e.r + 0.91958f * e.g + 0.01138f * e.b,
            0.01636f * e.r + 0.08799f * e.g + 0.89555f * e.b }}};
}

Color p3ToBt709(Color e) {
 return {{{ 1.22482f * e.r + -0.22490f * e.g + -0.00007f * e.b,
            -0.04196f * e.r + 1.04199f * e.g + 0.00001f * e.b,
            -0.01961f * e.r + -0.07865f * e.g + 1.09831f * e.b }}};
}

Color p3ToBt2100(Color e) {
 return {{{ 0.75378f * e.r + 0.19862f * e.g + 0.04754f * e.b,
            0.04576f * e.r + 0.94177f * e.g + 0.01250f * e.b,
            -0.00121f * e.r + 0.01757f * e.g + 0.98359f * e.b }}};
}

Color bt2100ToBt709(Color e) {
 return {{{ 1.66045f * e.r + -0.58764f * e.g + -0.07286f * e.b,
            -0.12445f * e.r + 1.13282f * e.g + -0.00837f * e.b,
            -0.01811f * e.r + -0.10057f * e.g + 1.11878f * e.b }}};
}

Color bt2100ToP3(Color e) {
 return {{{ 1.34369f * e.r + -0.28223f * e.g + -0.06135f * e.b,
            -0.06533f * e.r + 1.07580f * e.g + -0.01051f * e.b,
            0.00283f * e.r + -0.01957f * e.g + 1.01679f * e.b
 }}};
}

// TODO: confirm we always want to convert like this before calculating
// luminance.
ColorTransformFn getHdrConversionFn(jpegr_color_gamut sdr_gamut, jpegr_color_gamut hdr_gamut) {
    switch (sdr_gamut) {
    case JPEGR_COLORGAMUT_BT709:
      switch (hdr_gamut) {
        case JPEGR_COLORGAMUT_BT709:
          return identityConversion;
        case JPEGR_COLORGAMUT_P3:
          return p3ToBt709;
        case JPEGR_COLORGAMUT_BT2100:
          return bt2100ToBt709;
        case JPEGR_COLORGAMUT_UNSPECIFIED:
          return nullptr;
      }
      break;
    case JPEGR_COLORGAMUT_P3:
      switch (hdr_gamut) {
        case JPEGR_COLORGAMUT_BT709:
          return bt709ToP3;
        case JPEGR_COLORGAMUT_P3:
          return identityConversion;
        case JPEGR_COLORGAMUT_BT2100:
          return bt2100ToP3;
        case JPEGR_COLORGAMUT_UNSPECIFIED:
          return nullptr;
      }
      break;
    case JPEGR_COLORGAMUT_BT2100:
      switch (hdr_gamut) {
        case JPEGR_COLORGAMUT_BT709:
          return bt709ToBt2100;
        case JPEGR_COLORGAMUT_P3:
          return p3ToBt2100;
        case JPEGR_COLORGAMUT_BT2100:
          return identityConversion;
        case JPEGR_COLORGAMUT_UNSPECIFIED:
          return nullptr;
      }
      break;
    case JPEGR_COLORGAMUT_UNSPECIFIED:
      return nullptr;
  }
}


////////////////////////////////////////////////////////////////////////////////
// Recovery map calculations

uint8_t encodeRecovery(float y_sdr, float y_hdr, float hdr_ratio) {
  float gain = 1.0f;
  if (y_sdr > 0.0f) {
    gain = y_hdr / y_sdr;
  }

  if (gain < -hdr_ratio) gain = -hdr_ratio;
  if (gain > hdr_ratio) gain = hdr_ratio;

  return static_cast<uint8_t>(log2(gain) / log2(hdr_ratio) * 127.5f  + 127.5f);
}

static float applyRecovery(float e, float recovery, float hdr_ratio) {
  return exp2(log2(e) + recovery * log2(hdr_ratio));
}

Color applyRecovery(Color e, float recovery, float hdr_ratio) {
  return {{{ applyRecovery(e.r, recovery, hdr_ratio),
             applyRecovery(e.g, recovery, hdr_ratio),
             applyRecovery(e.b, recovery, hdr_ratio) }}};
}

// TODO: do we need something more clever for filtering either the map or images
// to generate the map?

static size_t clamp(const size_t& val, const size_t& low, const size_t& high) {
  return val < low ? low : (high < val ? high : val);
}

static float mapUintToFloat(uint8_t map_uint) {
  return (static_cast<float>(map_uint) - 127.5f) / 127.5f;
}

float sampleMap(jr_uncompressed_ptr map, size_t map_scale_factor, size_t x, size_t y) {
  float x_map = static_cast<float>(x) / static_cast<float>(map_scale_factor);
  float y_map = static_cast<float>(y) / static_cast<float>(map_scale_factor);

  size_t x_lower = static_cast<size_t>(floor(x_map));
  size_t x_upper = x_lower + 1;
  size_t y_lower = static_cast<size_t>(floor(y_map));
  size_t y_upper = y_lower + 1;

  x_lower = clamp(x_lower, 0, map->width - 1);
  x_upper = clamp(x_upper, 0, map->width - 1);
  y_lower = clamp(y_lower, 0, map->height - 1);
  y_upper = clamp(y_upper, 0, map->height - 1);

  float x_influence = x_map - static_cast<float>(x_lower);
  float y_influence = y_map - static_cast<float>(y_lower);

  float e1 = mapUintToFloat(reinterpret_cast<uint8_t*>(map->data)[x_lower + y_lower * map->width]);
  float e2 = mapUintToFloat(reinterpret_cast<uint8_t*>(map->data)[x_lower + y_upper * map->width]);
  float e3 = mapUintToFloat(reinterpret_cast<uint8_t*>(map->data)[x_upper + y_lower * map->width]);
  float e4 = mapUintToFloat(reinterpret_cast<uint8_t*>(map->data)[x_upper + y_upper * map->width]);

  return e1 * (x_influence + y_influence) / 2.0f
      + e2 * (x_influence + 1.0f - y_influence) / 2.0f
      + e3 * (1.0f - x_influence + y_influence) / 2.0f
      + e4 * (1.0f - x_influence + 1.0f - y_influence) / 2.0f;
}

Color getYuv420Pixel(jr_uncompressed_ptr image, size_t x, size_t y) {
  size_t pixel_count = image->width * image->height;

  size_t pixel_y_idx = x + y * image->width;
  size_t pixel_uv_idx = x / 2 + (y / 2) * (image->width / 2);

  uint8_t y_uint = reinterpret_cast<uint8_t*>(image->data)[pixel_y_idx];
  uint8_t u_uint = reinterpret_cast<uint8_t*>(image->data)[pixel_count + pixel_uv_idx];
  uint8_t v_uint = reinterpret_cast<uint8_t*>(image->data)[pixel_count * 5 / 4 + pixel_uv_idx];

  // 128 bias for UV given we are using jpeglib; see:
  // https://github.com/kornelski/libjpeg/blob/master/structure.doc
  return {{{ static_cast<float>(y_uint) / 255.0f,
             (static_cast<float>(u_uint) - 128.0f) / 255.0f,
             (static_cast<float>(v_uint) - 128.0f) / 255.0f }}};
}

Color getP010Pixel(jr_uncompressed_ptr image, size_t x, size_t y) {
  size_t pixel_count = image->width * image->height;

  size_t pixel_y_idx = x + y * image->width;
  size_t pixel_uv_idx = x / 2 + (y / 2) * (image->width / 2);

  uint16_t y_uint = reinterpret_cast<uint16_t*>(image->data)[pixel_y_idx]
                  >> 6;
  uint16_t u_uint = reinterpret_cast<uint16_t*>(image->data)[pixel_count + pixel_uv_idx * 2]
                  >> 6;
  uint16_t v_uint = reinterpret_cast<uint16_t*>(image->data)[pixel_count + pixel_uv_idx * 2 + 1]
                  >> 6;

  // Conversions include taking narrow-range into account.
  return {{{ static_cast<float>(y_uint) / 940.0f,
             (static_cast<float>(u_uint) - 64.0f) / 940.0f - 0.5f,
             (static_cast<float>(v_uint) - 64.0f) / 940.0f - 0.5f }}};
}

typedef Color (*getPixelFn)(jr_uncompressed_ptr, size_t, size_t);

static Color samplePixels(jr_uncompressed_ptr image, size_t map_scale_factor, size_t x, size_t y,
                          getPixelFn get_pixel_fn) {
  Color e = {{{ 0.0f, 0.0f, 0.0f }}};
  for (size_t dy = 0; dy < map_scale_factor; ++dy) {
    for (size_t dx = 0; dx < map_scale_factor; ++dx) {
      e += get_pixel_fn(image, x * map_scale_factor + dx, y * map_scale_factor + dy);
    }
  }

  return e / static_cast<float>(map_scale_factor * map_scale_factor);
}

Color sampleYuv420(jr_uncompressed_ptr image, size_t map_scale_factor, size_t x, size_t y) {
  return samplePixels(image, map_scale_factor, x, y, getYuv420Pixel);
}

Color sampleP010(jr_uncompressed_ptr image, size_t map_scale_factor, size_t x, size_t y) {
  return samplePixels(image, map_scale_factor, x, y, getP010Pixel);
}

uint32_t colorToRgba1010102(Color e_gamma) {
  return (0x3ff & static_cast<uint32_t>(e_gamma.r * 1023.0f))
       | ((0x3ff & static_cast<uint32_t>(e_gamma.g * 1023.0f)) << 10)
       | ((0x3ff & static_cast<uint32_t>(e_gamma.b * 1023.0f)) << 20)
       | (0x3 << 30);  // Set alpha to 1.0
}

} // namespace android::recoverymap