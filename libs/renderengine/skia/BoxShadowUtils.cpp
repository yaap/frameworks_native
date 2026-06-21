#include "BoxShadowUtils.h"

#include <SkRegion.h>
#include <SkVertices.h>
#include <common/trace.h>
#include <algorithm>

namespace android::renderengine::skia {

const SkString kEffectSource_BoxShadowEffect(R"(
uniform half2  u_rectCenter;
uniform half2  u_innerHalfDims;
uniform half2  u_radii; // x=key, y=ambient
uniform half2  u_invSigmas; // x=key, y=ambient

uniform half4  u_keyShadowColor;
uniform half2  u_keyOffset;

uniform half4  u_ambientShadowColor;
uniform half2  u_ambientOffset;

half sdRoundRect(half2 p, half2 d, half r) {
    half2 q = abs(p) - d;
    return min(max(q.x, q.y), half(0.0)) + length(max(q, half2(0.0))) - r;
}

half erf(half x) {
    return sign(x)*sqrt(half(1.0)-exp2(half(-1.78776)*x*x));
}

// Gaussian blur in 1D looks good enough.
half shadow(half dist, half invSigma) {
    return half(0.5)*(half(1.0) - erf(dist * invSigma));
}

half4 main(float2 fragCoord) {
    // Ambient Shadow Calculation
    half2 ambientP = fragCoord - u_rectCenter - u_ambientOffset;
    half ambientDist = sdRoundRect(ambientP, u_innerHalfDims, u_radii.y);
    half ambientIntensity = shadow(ambientDist, u_invSigmas.y);
    half4 ambientColor = u_ambientShadowColor * ambientIntensity;

    // Key Shadow Calculation
    half2 keyP = fragCoord - u_rectCenter - u_keyOffset;
    half keyDist = sdRoundRect(keyP, u_innerHalfDims, u_radii.x);
    half keyIntensity = shadow(keyDist, u_invSigmas.x);
    half4 keyColor = u_keyShadowColor * keyIntensity;

    // Blend the two shadow colors (standard src-over)
    return keyColor + ambientColor * (half(1.0) - keyColor.a);
}
)");

static sk_sp<SkVertices> getShadowFrameVertices(const SkRect& outer, const SkRect& inner) {
    SkPoint positions[8] = {{outer.fLeft, outer.fTop},     {outer.fRight, outer.fTop},
                            {outer.fRight, outer.fBottom}, {outer.fLeft, outer.fBottom},
                            {inner.fLeft, inner.fTop},     {inner.fRight, inner.fTop},
                            {inner.fRight, inner.fBottom}, {inner.fLeft, inner.fBottom}};

    static const uint16_t kIndices[24] = {
            0, 1, 5, 0, 5, 4, // Top
            1, 2, 6, 1, 6, 5, // Right
            2, 3, 7, 2, 7, 6, // Bottom
            3, 0, 4, 3, 4, 7  // Left
    };

    return SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, 8, positions, positions, nullptr,
                                24, kIndices);
}

BoxShadowUtils::BoxShadowUtils(RuntimeEffectManager& manager) : mManager(manager) {}

static float getInvSigma(float blurRadius) {
    float sigma = 0.57735f * blurRadius + 0.5f;
    return 1.0f / (sigma * 1.414213f);
};

void BoxShadowUtils::drawBoxShadows(SkCanvas* canvas, const SkRect& rect, float cornerRadius,
                                    const android::gui::BoxShadowSettings& settings,
                                    bool supportsFpk, bool isInteriorOccluded,
                                    const sk_sp<SkColorFilter>& colorTransform) {
    SFTRACE_CALL();

    if (settings.boxShadows.size() == 0) {
        return;
    }

    sk_sp<SkRuntimeEffect> effect = mManager.mKnownEffects[kBoxShadowEffect];

    android::gui::BoxShadowSettings::BoxShadowParams keyParams = settings.boxShadows[0];
    android::gui::BoxShadowSettings::BoxShadowParams ambientParams;
    if (settings.boxShadows.size() > 1) {
        ambientParams = settings.boxShadows[1];
    }

    // Calculate the total bounding box needed to draw both shadows.
    SkRect keyShadowRect = rect.makeOutset(keyParams.spreadRadius, keyParams.spreadRadius)
                                   .makeOffset(keyParams.offsetX, keyParams.offsetY);

    float keyOutset = convertBlurUserRadiusToSigma(keyParams.blurRadius) * 3.0f;
    SkRect keyDrawRect = keyShadowRect.makeOutset(keyOutset, keyOutset);

    SkRect ambientShadowRect =
            rect.makeOutset(ambientParams.spreadRadius, ambientParams.spreadRadius)
                    .makeOffset(ambientParams.offsetX, ambientParams.offsetY);
    float ambientOutset = convertBlurUserRadiusToSigma(ambientParams.blurRadius) * 3.0f;
    SkRect ambientDrawRect = ambientShadowRect.makeOutset(ambientOutset, ambientOutset);

    SkRect unionDrawRect = keyDrawRect;
    unionDrawRect.join(ambientDrawRect);

    // Set up the shader
    SkRuntimeShaderBuilder builder(effect);

    float halfWidth = rect.width() / 2.0f;
    float halfHeight = rect.height() / 2.0f;
    builder.uniform("u_rectCenter") = SkV2{rect.centerX(), rect.centerY()};
    builder.uniform("u_innerHalfDims") = SkV2{halfWidth - cornerRadius, halfHeight - cornerRadius};
    builder.uniform("u_radii") =
            SkV2{cornerRadius + keyParams.spreadRadius, cornerRadius + ambientParams.spreadRadius};

    builder.uniform("u_invSigmas") =
            SkV2{getInvSigma(keyParams.blurRadius), getInvSigma(ambientParams.blurRadius)};

    builder.uniform("u_keyShadowColor") = SkColor4f::FromColor(keyParams.color);
    builder.uniform("u_keyOffset") = SkV2{keyParams.offsetX, keyParams.offsetY};

    builder.uniform("u_ambientShadowColor") = SkColor4f::FromColor(ambientParams.color);
    builder.uniform("u_ambientOffset") = SkV2{ambientParams.offsetX, ambientParams.offsetY};

    SkPaint shadowPaint;
    shadowPaint.setAntiAlias(false);
    shadowPaint.setColorFilter(colorTransform);
    shadowPaint.setShader(builder.makeShader());

    if (isInteriorOccluded) {
        float kSin45Deg = 0.70710678118f;
        float inset = (1.0f - kSin45Deg) * cornerRadius;
        SkRect killRect = rect.makeInset(inset, inset);

        // Must be pixel aligned to generate glClear
        killRect.fLeft = ceilf(killRect.fLeft);
        killRect.fTop = ceilf(killRect.fTop);
        killRect.fRight = floorf(killRect.fRight);
        killRect.fBottom = floorf(killRect.fBottom);

        sk_sp<SkVertices> vertices = getShadowFrameVertices(unionDrawRect, killRect);
        canvas->drawVertices(vertices, SkBlendMode::kSrcOver, shadowPaint);

        if (supportsFpk) {
            SFTRACE_NAME("FPKOptimization");
            // This optimization is just for Ganesh and can be removed once graphite is
            // enabled.
            // E.g. on ARM Mali-G710 MP7 with 4 chrome windows open this
            // reduces GPU work period from 16ms to 10ms.
            SkPaint paint;
            paint.setAntiAlias(false);
            paint.setColor(0);
            paint.setBlendMode(SkBlendMode::kSrc);
            canvas->drawRect(killRect, paint);
        }
    } else {
        // Draw the combined shadow in a single draw call.
        canvas->drawRect(unionDrawRect, shadowPaint);
    }
}

void BoxShadowUtils::drawBorder(SkCanvas* canvas, const SkRect& rect, float cornerRadius,
                                const SkColor4f& color, float borderWidth,
                                const sk_sp<SkColorFilter>& colorTransform) {
    SFTRACE_CALL();
    if (borderWidth <= 0) return;

    float r = cornerRadius;
    if (r > rect.width() * 0.5f) r = rect.width() * 0.5f;
    if (r > rect.height() * 0.5f) r = rect.height() * 0.5f;

    // Prepare color and transparent color for fringe
    SkColor c = color.toSkColor();
    SkColor c0 = SkColorSetA(c, 0);

    float x0 = rect.fLeft;
    float y0 = rect.fTop;
    float x1 = rect.fRight;
    float y1 = rect.fBottom;

    // Corner centers
    SkPoint cc[4] = {
            {x1 - r, y0 + r}, // top-right
            {x1 - r, y1 - r}, // bottom-right
            {x0 + r, y1 - r}, // bottom-left
            {x0 + r, y0 + r}, // top-left
    };

    float sa[4] = {-SK_ScalarPI * 0.5f, 0, SK_ScalarPI * 0.5f, SK_ScalarPI};
    const int kNumSegs = std::clamp((int)ceilf(r * 0.5f), 1, 16);

    // Number of points in the perimeter path (4 corners * (segments + 1))
    int nPerim = 4 * (kNumSegs + 1);

    // We need 4 vertices per perimeter point for the stroke:
    // 0: Outer fringe edge (alpha 0)
    // 1: Outer stroke edge (alpha full)
    // 2: Inner stroke edge (alpha full)
    // 3: Inner fringe edge (alpha 0)
    int vertexCount = nPerim * 4;

    // 6 indices per quad segment (2 triangles * 3 vertices).
    // There are 3 strips (outer fringe, stroke body, inner fringe).
    // So 3 strips * 2 triangles * 3 indices = 18 indices per segment step.
    int indexCount = nPerim * 18;

    // Max segments is 16.
    // Max nPerim = 4 * (16 + 1) = 68.
    // Max vertexCount = 68 * 4 = 272.
    // Max indexCount = 68 * 18 = 1224.
    SkPoint pos[272];
    SkColor colors[272];
    uint16_t indices[1224];

    int vIndex = 0;

    float outset = 0.5f;              // half-pixel for AA fringe center
    float inset = borderWidth - 0.5f; // inner edge of opaque stroke

    for (int cIdx = 0; cIdx < 4; cIdx++) {
        for (int s = 0; s <= kNumSegs; s++) {
            float a = sa[cIdx] + (SK_ScalarPI * 0.5f) * (float)s / (float)kNumSegs;
            float nx = cosf(a);
            float ny = sinf(a);

            // Point on the rect boundary
            float px = cc[cIdx].fX + nx * r;
            float py = cc[cIdx].fY + ny * r;

            // 0: Outer fringe edge (transparent) -
            pos[vIndex] = {px + nx * outset, py + ny * outset};
            colors[vIndex] = c0;
            vIndex++;

            // 1: Outer stroke edge (opaque)
            pos[vIndex] = {px - nx * 0.5f, py - ny * 0.5f};
            colors[vIndex] = c;
            vIndex++;

            // 2: Inner stroke edge (opaque)
            pos[vIndex] = {px - nx * inset, py - ny * inset};
            colors[vIndex] = c;
            vIndex++;

            // 3: Inner fringe edge (transparent)
            pos[vIndex] = {px - nx * (borderWidth + 0.5f), py - ny * (borderWidth + 0.5f)};
            colors[vIndex] = c0;
            vIndex++;
        }
    }

    int idx = 0;
    for (int i = 0; i < nPerim; i++) {
        int next = (i + 1) % nPerim;

        int base = i * 4;
        int nextBase = next * 4;

        // Strip 1: Outer fringe (0 -> 1)
        indices[idx++] = base + 0;
        indices[idx++] = nextBase + 0;
        indices[idx++] = base + 1;
        indices[idx++] = nextBase + 0;
        indices[idx++] = nextBase + 1;
        indices[idx++] = base + 1;

        // Strip 2: Stroke body (1 -> 2)
        indices[idx++] = base + 1;
        indices[idx++] = nextBase + 1;
        indices[idx++] = base + 2;
        indices[idx++] = nextBase + 1;
        indices[idx++] = nextBase + 2;
        indices[idx++] = base + 2;

        // Strip 3: Inner fringe (2 -> 3)
        indices[idx++] = base + 2;
        indices[idx++] = nextBase + 2;
        indices[idx++] = base + 3;
        indices[idx++] = nextBase + 2;
        indices[idx++] = nextBase + 3;
        indices[idx++] = base + 3;
    }

    sk_sp<SkVertices> vertices =
            SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, vertexCount, pos, nullptr,
                                 colors, indexCount, indices);

    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setBlendMode(SkBlendMode::kSrcOver);
    paint.setAntiAlias(false);
    paint.setColorFilter(colorTransform);
    canvas->drawVertices(vertices, SkBlendMode::kModulate, paint);
}

} // namespace android::renderengine::skia
