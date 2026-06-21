/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <SkCanvas.h>
#include <SkData.h>
#include <SkEncodedImageFormat.h>
#include <SkImage.h>
#include <SkPngEncoder.h>
#include <SkSurface.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <android/ipcrenderbuffer/RenderBufferHelpers.h>

#include <getopt.h>
#include <gui/RenderCommandBuffer.h>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>

using namespace android;

bool renderCommandBufferToReplayCanvas(IPCServerResourceCache* cache, RenderCommandBuffer* buffer,
                                       SkCanvas* canvas,
                                       const std::function<void(int)>& renderProxyCallback,
                                       bool dumpOps, bool noBitmaps) {
    bool foundFirstDrawingOp = false;

    if (dumpOps) {
        ALOGE("Rendering command buffer");
    }

    for (IPCRenderBufferOp* op = buffer->getOps(); op; op = op->next) {
        if (dumpOps) {
            ALOGE("Rendering op %s", opTypeToString(op->type).c_str());
            ALOGE("Details %s", opToString(op).c_str());
        }
        if (op->type == DrawImageRectOp::kType) {
            if (noBitmaps) {
                auto* imageRectOp = static_cast<DrawImageRectOp*>(op);
                SkPaint p;
                p.setColor(SkColorSetARGB(0xFF, rand() % 255, rand() % 255, rand() % 255));
                canvas->drawRect(imageRectOp->dst, p);
            } else {
                LOG_ALWAYS_FATAL("Bitmap rendering not yet supported in replay");
            }
        } else {
            renderOpToCanvas(cache, op, canvas, renderProxyCallback);
        }
    }
    return true;
}

int main(int argc, char** argv) {
    srand(time(nullptr));
    bool dumpOps = false;
    bool noBitmaps = true;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"dump-ops", no_argument, 0, 'd'},
        {"no-bitmaps", no_argument, 0, 'n'},
        {"bitmaps", no_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "hdnb", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: replay_render_buffer [options] <command_buffer_file> <bitmap_arena_file>\n");
                printf("Options:\n");
                printf("  -h, --help            Show this help message\n");
                printf("  -d, --dump-ops        Dump render buffer ops to logcat\n");
                printf("  -n, --no-bitmaps      Interpret DrawImageRect commands as drawing a random color (default)\n");
                printf("  -b, --bitmaps         Render bitmaps (not yet supported)\n");
                return 0;
            case 'd':
                dumpOps = true;
                break;
            case 'n':
                noBitmaps = true;
                break;
            case 'b':
                noBitmaps = false;
                break;
            default: /* '?' */
                printf("Usage: replay_render_buffer [options] <command_buffer_file> <bitmap_arena_file>\n");
                return 1;
        }
    }
    if (optind + 1 >= argc) {
        printf("Usage: replay_render_buffer <command_buffer_file> <bitmap_arena_file>\n");
        return 1;
    }
    const char* commandBufferFile = argv[optind];

    // Load from files
    std::unique_ptr<RenderCommandBuffer> loadedCommandBuffer = std::unique_ptr<RenderCommandBuffer>(RenderCommandBuffer::loadFromFile(commandBufferFile));
    if (!loadedCommandBuffer) {
        printf("Failed to load from files");
        return 1;
    }

    SkImageInfo info = SkImageInfo::MakeN32Premul(512, 512); // Example size
    const size_t minRowBytes = info.minRowBytes();
    const size_t size = info.computeMinByteSize();
    SkPMColor* pixels = new SkPMColor[size];

    // Create SkSurface using SkSurfaces::WrapPixels
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels, minRowBytes);
    if (!surface) {
        ALOGE("Failed to create SkSurface with SkSurfaces::WrapPixels");
        delete[] pixels;
        return 1;
    }
    SkCanvas* canvas = surface->getCanvas(); // Get Canvas from the Surface

    canvas->clear(SK_ColorWHITE); // Example background

    // Replay the render commands
    IPCServerResourceCache cache;
    renderCommandBufferToCanvas(&cache, loadedCommandBuffer.get(), canvas,
                                [&](int) {}); // Pass the canvas obtained from the surface

    // Now you can display the surface's image or save it to a file.
    // For example, to save to a PNG:
    sk_sp<SkImage> image = surface->makeImageSnapshot(); // Create image from surface
    if (!image) {
        ALOGE("Failed to create image snapshot");
        return 1;
    }

    SkPngEncoder::Options options; // Use default options for now
    sk_sp<SkData> pngData =
            SkPngEncoder::Encode(nullptr, image.get(), options); // Encode using SkPngEncoder
    if (pngData) {
        std::ofstream pngFile("/data/replay.png", std::ios::binary);
        pngFile.write(reinterpret_cast<const char*>(pngData->data()), pngData->size());
        pngFile.close();
        ALOGE("Replay saved to /data/replay.png");
    } else {
        ALOGE("Failed to encode to PNG using SkPngEncoder");
    }

    delete[] pixels; // Delete allocated pixels
    return 0;
}

#pragma clang diagnostic pop
