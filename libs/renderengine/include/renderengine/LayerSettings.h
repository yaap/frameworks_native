/*
 * Copyright 2018 The Android Open Source Project
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

#pragma once

#include <SkRuntimeEffect.h>
#include <android/gui/BorderSettings.h>
#include <android/gui/BoxShadowSettings.h>
#include <com_android_graphics_surfaceflinger_flags.h>
#include <ftl/enum.h>
#include <gui/CornerRadii.h>
#include <gui/DisplayLuts.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <renderengine/ExternalTexture.h>
#include <renderengine/PrintUtils.h>
#include <ui/BlurRegion.h>
#include <ui/DebugUtils.h>
#include <ui/Fence.h>
#include <ui/FloatRect.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicTypes.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <ui/ShadowSettings.h>
#include <ui/StretchEffect.h>
#include <ui/Transform.h>
#include "ui/EdgeExtensionEffect.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <vector>

/**
 * WARNING: do NOT change default values of existing types, as existing tests and benchmarks rely
 * on default behavior.
 */

namespace android {

struct IPCServerResourceCache;
class RenderCommandBuffer;

namespace renderengine {

// Metadata describing the input buffer to render from.
struct Buffer {
    // Buffer containing the image that we will render.
    // If buffer == nullptr, then the rest of the fields in this struct will be
    // ignored.
    std::shared_ptr<ExternalTexture> buffer = nullptr;

    // Fence that will fire when the buffer is ready to be bound.
    sp<Fence> fence = nullptr;

    // Whether to use filtering when rendering the texture.
    bool useTextureFiltering = false;

    // Transform matrix to apply to texture coordinates.
    mat4 textureTransform = mat4();

    // Whether to use pre-multiplied alpha.
    bool usePremultipliedAlpha = true;

    // Override flag that alpha for each pixel in the buffer *must* be 1.0.
    // LayerSettings::alpha is still used if isOpaque==true - this flag only
    // overrides the alpha channel of the buffer.
    bool isOpaque = false;

    float maxLuminanceNits = 0.0;
};

// Metadata describing the layer geometry.
struct Geometry {
    // Boundaries of the layer.
    FloatRect boundaries = FloatRect();

    // Boundaries of the layer before transparent region hint is subtracted.
    // Effects like shadows and outline ignore the transparent region hint.
    FloatRect originalBounds = FloatRect();

    // Transform matrix to apply to mesh coordinates.
    mat4 positionTransform = mat4();

    // Radius of rounded corners, if greater than 0. Otherwise, this layer's
    // corners are not rounded.
    // Having corner radius will force GPU composition on the layer and its children, drawing it
    // with a special shader. The shader will receive the radius and the crop rectangle as input,
    // modifying the opacity of the destination texture, multiplying it by a number between 0 and 1.
    // We query Layer#getRoundedCornerState() to retrieve the radius as well as the rounded crop
    // rectangle to figure out how to apply the radius for this layer. The crop rectangle will be
    // in local layer coordinate space, so we have to take the layer transform into account when
    // walking up the tree.
    gui::CornerRadii roundedCornersRadii = {};

    // Rectangle within which corners will be rounded.
    FloatRect roundedCornersCrop = FloatRect();

    // Crop geometry in local space, used for cropping outset rendering, e.g. shadows.
    gui::CornerRadii otherRoundedCornersRadii = {};

    FloatRect otherCrop = FloatRect();
};

// Descriptor of the source pixels for this layer.
struct PixelSource {
    // Source buffer
    Buffer buffer = Buffer();

    // The solid color with which to fill the layer.
    // This should only be populated if we don't render from an application
    // buffer.
    half3 solidColor = half3(0.0f, 0.0f, 0.0f);
};

// The settings that RenderEngine requires for correctly rendering a Layer.
struct LayerSettings {
    // Geometry information
    Geometry geometry = Geometry();

    // Source pixels for this layer.
    PixelSource source = PixelSource();

    // Alpha option to blend with the source pixels
    half alpha = half(0.0);

    // Color space describing how the source pixels should be interpreted.
    ui::Dataspace sourceDataspace = ui::Dataspace::UNKNOWN;

    // Additional layer-specific color transform to be applied before the global
    // transform.
    mat4 colorTransform = mat4();

    // True if blending will be forced to be disabled.
    bool disableBlending = false;

    // If true, then this layer casts a shadow and/or blurs behind it, but it does
    // not otherwise draw any of the layer's other contents.
    bool skipContentDraw = false;

    ShadowSettings shadow;

    gui::BorderSettings borderSettings;

    gui::BoxShadowSettings boxShadowSettings;

    int backgroundBlurRadius = 0;
    float backgroundBlurScale = 1.0f;

    std::vector<BlurRegion> blurRegions;

    // Transform matrix used to convert the blurRegions geometry into the same
    // coordinate space as LayerSettings.geometry
    mat4 blurRegionTransform = mat4();

    StretchEffect stretchEffect;
    EdgeExtensionEffect edgeExtensionEffect;

    // Name associated with the layer for debugging purposes.
    std::string name;

    // Luminance of the white point for this layer. Used for linear dimming.
    // Individual layers will be dimmed by (whitePointNits / maxWhitePoint).
    // If white point nits are unknown, then this layer is assumed to have the
    // same luminance as the brightest layer in the scene.
    float whitePointNits = -1.f;

    std::shared_ptr<gui::DisplayLuts> luts;

    std::shared_ptr<IPCServerResourceCache> renderResourceCache;
    std::shared_ptr<RenderCommandBuffer> renderCommandBuffer;

    // A raw runtime effect and its uniforms.
    sk_sp<SkRuntimeEffect> postProcessEffect;
    std::shared_ptr<std::vector<uint8_t>> postProcessUniforms;

    enum class SampleTarget : uint32_t {
        Self,
        Behind,
        ftl_last = Behind,
    };
    SampleTarget postProcessTarget;
};

// Keep in sync with custom comparison function in
// compositionengine/impl/ClientCompositionRequestCache.cpp
static inline bool operator==(const Buffer& lhs, const Buffer& rhs) {
    return lhs.buffer == rhs.buffer && lhs.fence == rhs.fence &&
            lhs.useTextureFiltering == rhs.useTextureFiltering &&
            lhs.textureTransform == rhs.textureTransform &&
            lhs.usePremultipliedAlpha == rhs.usePremultipliedAlpha &&
            lhs.isOpaque == rhs.isOpaque && lhs.maxLuminanceNits == rhs.maxLuminanceNits;
}

static inline bool operator==(const Geometry& lhs, const Geometry& rhs) {
    return lhs.boundaries == rhs.boundaries && lhs.positionTransform == rhs.positionTransform &&
            lhs.roundedCornersRadii == rhs.roundedCornersRadii &&
            lhs.roundedCornersCrop == rhs.roundedCornersCrop;
}

static inline bool operator==(const PixelSource& lhs, const PixelSource& rhs) {
    return lhs.buffer == rhs.buffer && lhs.solidColor == rhs.solidColor;
}

static inline bool operator==(const LayerSettings& lhs, const LayerSettings& rhs) {
    if (lhs.blurRegions.size() != rhs.blurRegions.size()) {
        return false;
    }
    const auto size = lhs.blurRegions.size();
    for (size_t i = 0; i < size; i++) {
        if (lhs.blurRegions[i] != rhs.blurRegions[i]) {
            return false;
        }
    }

    return lhs.geometry == rhs.geometry && lhs.source == rhs.source && lhs.alpha == rhs.alpha &&
            lhs.sourceDataspace == rhs.sourceDataspace &&
            lhs.colorTransform == rhs.colorTransform &&
            lhs.disableBlending == rhs.disableBlending &&
            lhs.skipContentDraw == rhs.skipContentDraw && lhs.shadow == rhs.shadow &&
            lhs.backgroundBlurRadius == rhs.backgroundBlurRadius &&
            lhs.backgroundBlurScale == rhs.backgroundBlurScale &&
            lhs.blurRegionTransform == rhs.blurRegionTransform &&
            lhs.stretchEffect == rhs.stretchEffect &&
            lhs.edgeExtensionEffect == rhs.edgeExtensionEffect &&
            lhs.whitePointNits == rhs.whitePointNits && lhs.luts == rhs.luts;
}

static inline void PrintTo(const Buffer& settings, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "Buffer {";
    *os << newline << ".buffer = " << *settings.buffer.get();
    *os << newline << ".fence = " << settings.fence.get();
    *os << newline << ".useTextureFiltering = " << settings.useTextureFiltering;
    *os << newline << ".textureTransform = ";
    PrintMatrix(settings.textureTransform, os);
    *os << newline << ".usePremultipliedAlpha = " << settings.usePremultipliedAlpha;
    *os << newline << ".isOpaque = " << settings.isOpaque;
    *os << newline << ".maxLuminanceNits = " << settings.maxLuminanceNits;
    *os << IndentedNewline(currentIndent) << "}";
}

static inline void PrintTo(const Geometry& settings, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "Geometry {";
    *os << newline << ".boundaries = ";
    PrintTo(settings.boundaries, os);
    *os << newline << ".originalBounds = ";
    PrintTo(settings.originalBounds, os);
    *os << newline << ".positionTransform = ";
    PrintMatrix(settings.positionTransform, os);
    *os << newline << ".roundedCornersRadii = " << settings.roundedCornersRadii;
    *os << newline << ".roundedCornersCrop = ";
    PrintTo(settings.roundedCornersCrop, os);
    *os << newline << ".otherRoundedCornersRadii = " << settings.otherRoundedCornersRadii;
    *os << newline << ".otherCrop = ";
    PrintTo(settings.otherCrop, os);

    *os << IndentedNewline(currentIndent) << "}";
}

static inline void PrintTo(const PixelSource& settings, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "PixelSource {";
    if (settings.buffer.buffer) {
        *os << newline << ".buffer = ";
        PrintTo(settings.buffer, os, currentIndent + 1);
        *os << IndentedNewline(currentIndent) << "}";
    } else {
        *os << newline << ".solidColor = " << settings.solidColor;
        *os << IndentedNewline(currentIndent) << "}";
    }
}

static inline void PrintTo(const ShadowSettings& settings, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "ShadowSettings {";
    *os << newline << ".boundaries = ";
    PrintTo(settings.boundaries, os);
    *os << newline << ".ambientColor = " << settings.ambientColor;
    *os << newline << ".spotColor = " << settings.spotColor;
    *os << newline << ".lightPos = " << settings.lightPos;
    *os << newline << ".lightRadius = " << settings.lightRadius;
    *os << newline << ".length = " << settings.length;
    *os << newline << ".casterIsTranslucent = " << settings.casterIsTranslucent;
    *os << IndentedNewline(currentIndent) << "}";
}

static inline void PrintTo(const StretchEffect& effect, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "StretchEffect {";
    *os << newline << ".width = " << effect.width;
    *os << newline << ".height = " << effect.height;
    *os << newline << ".vectorX = " << effect.vectorX;
    *os << newline << ".vectorY = " << effect.vectorY;
    *os << newline << ".maxAmountX = " << effect.maxAmountX;
    *os << newline << ".maxAmountY = " << effect.maxAmountY;
    *os << newline << ".mappedLeft = " << effect.mappedChildBounds.left;
    *os << newline << ".mappedTop = " << effect.mappedChildBounds.top;
    *os << newline << ".mappedRight = " << effect.mappedChildBounds.right;
    *os << newline << ".mappedBottom = " << effect.mappedChildBounds.bottom;
    *os << IndentedNewline(currentIndent) << "}";
}

static inline void PrintTo(const EdgeExtensionEffect& effect, ::std::ostream* os) {
    *os << effect;
}

// TODO: print the effect's name after it's exposed in Skia.
static inline void PrintTo(const sk_sp<SkRuntimeEffect> effect, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    const std::string innerNewline = IndentedNewline(currentIndent + 2);
    *os << "SkRuntimeEffect {";
    if (!effect->children().empty()) {
        *os << newline << ".children = [";
        for (const auto& child : effect->children()) {
            *os << innerNewline << child.name << " (type = " << static_cast<int>(child.type) << ")";
        }
        *os << newline << "]";
    }
    std::stringstream source(effect->source());
    std::string lineSrc;
    int lineNumber = 1;
    *os << newline << ".source = (";
    while (std::getline(source, lineSrc)) {
        // Printing line numbers gives an indication if some log lines are missing.
        *os << innerNewline << std::setw(4) << lineNumber++ << ": " << lineSrc;
    }
    *os << newline << ")";
    *os << IndentedNewline(currentIndent) << "}";
}

static inline void PrintTo(const LayerSettings& settings, ::std::ostream* os,
                           const uint8_t currentIndent = 0) {
    const std::string newline = IndentedNewline(currentIndent + 1);
    *os << "LayerSettings for '" << settings.name.c_str() << "' {";
    *os << newline << ".geometry = ";
    PrintTo(settings.geometry, os, currentIndent + 1);
    *os << newline << ".source = ";
    PrintTo(settings.source, os, currentIndent + 1);
    *os << newline << ".alpha = " << settings.alpha;
    *os << newline << ".sourceDataspace = ";
    PrintTo(settings.sourceDataspace, os);
    *os << " (" << ::dataspaceDetails(static_cast<android_dataspace>(settings.sourceDataspace))
        << ")";
    *os << newline << ".colorTransform = ";
    PrintMatrix(settings.colorTransform, os);
    *os << newline << ".disableBlending = " << settings.disableBlending;
    *os << newline << ".skipContentDraw = " << settings.skipContentDraw;
    if (settings.shadow != ShadowSettings()) {
        *os << newline << ".shadow = ";
        PrintTo(settings.shadow, os, currentIndent + 1);
    }
    if (settings.borderSettings != gui::BorderSettings()) {
        *os << newline << ".borderSettings = " << settings.borderSettings.toString();
    }
    if (settings.boxShadowSettings != gui::BoxShadowSettings()) {
        *os << newline << ".boxShadowSettings = " << settings.boxShadowSettings.toString();
    }
    *os << newline << ".backgroundBlurRadius = " << settings.backgroundBlurRadius;
    *os << newline << ".backgroundBlurScale = " << settings.backgroundBlurScale;
    if (settings.blurRegions.size()) {
        *os << newline << ".blurRegions = [";
        const std::string blurRegionNewline = IndentedNewline(currentIndent + 2);
        for (auto blurRegion : settings.blurRegions) {
            *os << blurRegionNewline;
            PrintTo(blurRegion, os, currentIndent + 2);
        }
        *os << newline << "]";
    }
    *os << newline << ".blurRegionTransform = ";
    PrintMatrix(settings.blurRegionTransform, os);
    if (settings.stretchEffect != StretchEffect()) {
        *os << newline << ".stretchEffect = ";
        PrintTo(settings.stretchEffect, os, currentIndent + 1);
    }
    if (settings.edgeExtensionEffect.hasEffect()) {
        *os << newline << ".edgeExtensionEffect = " << settings.edgeExtensionEffect;
    }
    *os << newline << ".whitePointNits = " << settings.whitePointNits;
    if (settings.luts) {
        *os << newline << ".luts = ";
        PrintTo(settings.luts, os, currentIndent + 1);
    }
    if (settings.renderCommandBuffer) {
        *os << newline << ".renderCommandBuffer = " << settings.renderCommandBuffer.get();
    }
    if (settings.postProcessEffect) {
        *os << newline << ".postProcessEffect = ";
        PrintTo(settings.postProcessEffect, os, currentIndent + 1);
    }
    if (settings.postProcessUniforms) {
        *os << newline << ".postProcessUniforms = " << "[ ";
        for (const uint8_t uniform : *settings.postProcessUniforms) {
            *os << uniform << " ";
        }
        *os << "]";
    }
    if (settings.postProcessEffect) {
        *os << newline << ".postProcessTarget = " << ftl::enum_string(settings.postProcessTarget);
    }

    *os << IndentedNewline(currentIndent) << "}";
}

} // namespace renderengine
} // namespace android
