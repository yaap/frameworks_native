/*
 * Copyright 2024 The Android Open Source Project
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

#include "GaneshBackendTexture.h"

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <include/core/SkImage.h>
#include <include/core/SkImageInfo.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/SkImageGanesh.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <include/gpu/ganesh/vk/GrVkBackendSurface.h>
#include <include/gpu/ganesh/vk/GrVkTypes.h>

#include <renderengine/ColorSpaces.h>
#include "skia/compat/SkiaBackendTexture.h"

#include <android/hardware_buffer.h>
#include <common/trace.h>
#include <log/log_main.h>

#include <format>

namespace android::renderengine::skia {

GaneshBackendTexture::GaneshBackendTexture(sk_sp<GrDirectContext> grContext,
                                           AHardwareBuffer* buffer, bool isOutputBuffer)
      : SkiaBackendTexture(isOutputBuffer), mGrContext(grContext) {
    SFTRACE_CALL();
    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(buffer, &desc);

    // Save this for use in makeImage() and makeSurface().
    mColorType = AHardwareBufferUtils::GetSkColorTypeFromBufferFormat(desc.format);

    const bool createProtectedImage = 0 != (desc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT);

    GrBackendFormat backendFormat;
    const GrBackendApi graphicsApi = grContext->backend();
    if (graphicsApi == GrBackendApi::kOpenGL) {
        backendFormat =
                GrAHardwareBufferUtils::GetGLBackendFormat(grContext.get(), desc.format, false);
        mBackendTexture =
                GrAHardwareBufferUtils::MakeGLBackendTexture(grContext.get(), buffer, desc.width,
                                                             desc.height, &mDeleteProc,
                                                             &mUpdateProc, &mImageCtx,
                                                             createProtectedImage, backendFormat,
                                                             isOutputBuffer);
    } else if (graphicsApi == GrBackendApi::kVulkan) {
        backendFormat = GrAHardwareBufferUtils::GetVulkanBackendFormat(grContext.get(), buffer,
                                                                       desc.format, false);
        mBackendTexture =
                GrAHardwareBufferUtils::MakeVulkanBackendTexture(grContext.get(), buffer,
                                                                 desc.width, desc.height,
                                                                 &mDeleteProc, &mUpdateProc,
                                                                 &mImageCtx, createProtectedImage,
                                                                 backendFormat, isOutputBuffer);
    } else {
        LOG_ALWAYS_FATAL("Unexpected graphics API %u", static_cast<unsigned>(graphicsApi));
    }

    if (!mBackendTexture.isValid() || !desc.width || !desc.height) {
        LOG_ALWAYS_FATAL("Failed to create a valid texture. [%p]:[%d,%d] isProtected:%d "
                         "isWriteable:%d format:%d",
                         this, desc.width, desc.height, createProtectedImage, isOutputBuffer,
                         desc.format);
    }
}

GaneshBackendTexture::~GaneshBackendTexture() {
    if (mBackendTexture.isValid()) {
        mDeleteProc(mImageCtx);
        mBackendTexture = {};
    }
}

sk_sp<SkImage> GaneshBackendTexture::makeImage(SkAlphaType alphaType, ui::Dataspace dataspace,
                                               TextureReleaseProc releaseImageProc,
                                               ReleaseContext releaseContext,
                                               ftl::Flags<ColorSpaceOptions> options) {
    if (mBackendTexture.isValid()) {
        mUpdateProc(mImageCtx, mGrContext.get());
    }

    SkColorType colorType = internalColorType();
    if (alphaType == kUnknown_SkAlphaType) {
        switch (colorType) {
            case kRGBA_8888_SkColorType:
                colorType = kRGB_888x_SkColorType;
                break;
            // TODO(skbug.com/12048): Use RGBx for f16 and 1010102. These fall through in order to
            // preserve the behavior from before ag/37959316 where the SkiaRenderEngine would
            // preemptively apply an opaque workaround for these color types.
            case kRGBA_F16_SkColorType:
            case kRGBA_1010102_SkColorType: [[fallthrough]];
            default:
                // No way to force this to opaque in Ganesh, so pretend the alpha type is premul
                // and SkiaRenderEngine will apply an opaque workaround.
                alphaType = kPremul_SkAlphaType;
                break;
        }
    }
    // Adjust alpha type to kOpaque_SkAlphaType if the color type does not have an alpha channel.
    if (SkColorTypeIsAlwaysOpaque(colorType)) {
        alphaType = kOpaque_SkAlphaType;
    }

    sk_sp<SkImage> image =
            SkImages::BorrowTextureFrom(mGrContext.get(), mBackendTexture, kTopLeft_GrSurfaceOrigin,
                                        colorType, alphaType, toSkColorSpace(dataspace, options),
                                        releaseImageProc, releaseContext);
    if (!image) {
        logFatalTexture("Unable to generate SkImage.", dataspace, colorType, alphaType);
    }
    return image;
}

sk_sp<SkSurface> GaneshBackendTexture::makeSurface(ui::Dataspace dataspace,
                                                   TextureReleaseProc releaseSurfaceProc,
                                                   ReleaseContext releaseContext,
                                                   ftl::Flags<ColorSpaceOptions> options) {
    const SkColorType colorType = internalColorType();
    sk_sp<SkSurface> surface =
            SkSurfaces::WrapBackendTexture(mGrContext.get(), mBackendTexture,
                                           kTopLeft_GrSurfaceOrigin, 0, colorType,
                                           toSkColorSpace(dataspace, options), nullptr,
                                           releaseSurfaceProc, releaseContext);
    if (!surface) {
        logFatalTexture("Unable to generate SkSurface.", dataspace, colorType, kPremul_SkAlphaType);
    }
    return surface;
}

std::string GaneshBackendTexture::backendDebugInfo() const {
    if (!mBackendTexture.isValid()) {
        return "GaneshBackendTexture(INVALID)";
    }
    return std::format("GaneshBackendTexture(dimensions={}x{}, internalColorType: {}, {}))",
                       mBackendTexture.dimensions().width(), mBackendTexture.dimensions().height(),
                       static_cast<int>(internalColorType()),
                       mBackendTexture.getLabel());
}

void GaneshBackendTexture::logFatalTexture(const char* msg, ui::Dataspace dataspace,
                                           SkColorType colorType, SkAlphaType alphaType) {
    switch (mBackendTexture.backend()) {
        case GrBackendApi::kOpenGL: {
            GrGLTextureInfo textureInfo;
            bool retrievedTextureInfo =
                    GrBackendTextures::GetGLTextureInfo(mBackendTexture, &textureInfo);
            LOG_ALWAYS_FATAL("%s isTextureValid:%d dataspace:%d"
                             "\n\tGrBackendTexture: (%i x %i) hasMipmaps: %i isProtected: %i "
                             "texType: %i\n\t\tGrGLTextureInfo: success: %i fTarget: %u fFormat: %u"
                             " colorType %i alphatype: %i",
                             msg, mBackendTexture.isValid(), static_cast<int32_t>(dataspace),
                             mBackendTexture.width(), mBackendTexture.height(),
                             mBackendTexture.hasMipmaps(), mBackendTexture.isProtected(),
                             static_cast<int>(mBackendTexture.textureType()), retrievedTextureInfo,
                             textureInfo.fTarget, textureInfo.fFormat, colorType, alphaType);
            break;
        }
        case GrBackendApi::kVulkan: {
            GrVkImageInfo imageInfo;
            bool retrievedImageInfo =
                    GrBackendTextures::GetVkImageInfo(mBackendTexture, &imageInfo);
            LOG_ALWAYS_FATAL("%s isTextureValid:%d dataspace:%d"
                             "\n\tGrBackendTexture: (%i x %i) hasMipmaps: %i isProtected: %i "
                             "texType: %i\n\t\tVkImageInfo: success: %i fFormat: %i "
                             "fSampleCount: %u fLevelCount: %u colorType %i alphaType: %i",
                             msg, mBackendTexture.isValid(), static_cast<int32_t>(dataspace),
                             mBackendTexture.width(), mBackendTexture.height(),
                             mBackendTexture.hasMipmaps(), mBackendTexture.isProtected(),
                             static_cast<int>(mBackendTexture.textureType()), retrievedImageInfo,
                             imageInfo.fFormat, imageInfo.fSampleCount, imageInfo.fLevelCount,
                             colorType, alphaType);
            break;
        }
        default:
            LOG_ALWAYS_FATAL("%s Unexpected backend %u", msg,
                             static_cast<unsigned>(mBackendTexture.backend()));
            break;
    }
}

} // namespace android::renderengine::skia
