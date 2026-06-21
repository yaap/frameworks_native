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

#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>

namespace android {

std::string opToString(IPCRenderBufferOp* op) {
    switch (op->type) {
        case TYPE_SAVE: {
            SaveOp* co = (SaveOp*)op;
            return co->toString();
            break;
        }
        case TYPE_RESTORE: {
            RestoreOp* co = (RestoreOp*)op;
            return co->toString();
            break;
        }
        case TYPE_SAVELAYER: {
            SaveLayerOp* co = (SaveLayerOp*)op;
            return co->toString();
            break;
        }
        case TYPE_SAVEBEHIND: {
            SaveBehindOp* co = (SaveBehindOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CONCAT: {
            ConcatOp* co = (ConcatOp*)op;
            return co->toString();
            break;
        }
        case TYPE_SETMATRIX: {
            SetMatrixOp* co = (SetMatrixOp*)op;
            return co->toString();
            break;
        }
        case TYPE_SCALE: {
            ScaleOp* co = (ScaleOp*)op;
            return co->toString();
            break;
        }
        case TYPE_TRANSLATE: {
            TranslateOp* co = (TranslateOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CLIPPATH: {
            ClipPathOp* co = (ClipPathOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CLIPRECT: {
            ClipRectOp* co = (ClipRectOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CLIPRRECT: {
            ClipRRectOp* co = (ClipRRectOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CLIPREGION: {
            ClipRegionOp* co = (ClipRegionOp*)op;
            return co->toString();
            break;
        }
        case TYPE_CLIPSHADER: {
            ClipShaderOp* co = (ClipShaderOp*)op;
            return co->toString();
            break;
        }
        case TYPE_RESETCLIP: {
            ResetClipOp* co = (ResetClipOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPAINT: {
            DrawPaintOp* co = (DrawPaintOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWBEHIND: {
            DrawBehindOp* co = (DrawBehindOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPATH: {
            DrawPathOp* co = (DrawPathOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWRECT: {
            DrawRectOp* co = (DrawRectOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWREGION: {
            DrawRegionOp* co = (DrawRegionOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWOVAL: {
            DrawOvalOp* co = (DrawOvalOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWARC: {
            DrawArcOp* co = (DrawArcOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWRRECT: {
            DrawRRectOp* co = (DrawRRectOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWANNOTATION: {
            DrawAnnotationOp* co = (DrawAnnotationOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWDRAWABLE: {
            DrawDrawableOp* co = (DrawDrawableOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPICTURE: {
            DrawPictureOp* co = (DrawPictureOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWIMAGE: {
            DrawImageOp* co = (DrawImageOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWIMAGERECT: {
            DrawImageRectOp* co = (DrawImageRectOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWTEXTBLOB: {
            DrawTextBlobOp* co = (DrawTextBlobOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPATCH: {
            DrawPatchOp* co = (DrawPatchOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPOINTS: {
            DrawPointsOp* co = (DrawPointsOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWVERTICES: {
            DrawVerticesOp* co = (DrawVerticesOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWSKMESH: {
            DrawSkMeshOp* co = (DrawSkMeshOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWATLAS: {
            DrawAtlasOp* co = (DrawAtlasOp*)op;
            return co->toString();
            break;
        }
        case TYPE_DRAWPROXYSURFACECONTROL: {
            DrawProxySurfaceControlOp* co = (DrawProxySurfaceControlOp*)op;
            return co->toString();
            break;
        }
        case TYPE_BEGINRENDERTARGET: {
            BeginRenderTargetOp* co = (BeginRenderTargetOp*)op;
            return co->toString();
            break;
        }
        case TYPE_ENDRENDERTARGET: {
            BeginRenderTargetOp* co = (BeginRenderTargetOp*)op;
            return co->toString();
            break;
        }
        default: {
            ALOGE("Unexpected op in RenderCommandBuffer");
            break;
        }
    }
    return "Unknown OP";
}

std::string opTypeToString(uint32_t type) {
    switch (type) {
        case TYPE_SAVE:
            return "TYPE_SAVE";
        case TYPE_RESTORE:
            return "TYPE_RESTORE";
        case TYPE_SAVELAYER:
            return "TYPE_SAVELAYER";
        case TYPE_SAVEBEHIND:
            return "TYPE_SAVEBEHIND";
        case TYPE_CONCAT:
            return "TYPE_CONCAT";
        case TYPE_SETMATRIX:
            return "TYPE_SETMATRIX";
        case TYPE_SCALE:
            return "TYPE_SCALE";
        case TYPE_TRANSLATE:
            return "TYPE_TRANSLATE";
        case TYPE_CLIPPATH:
            return "TYPE_CLIPPATH";
        case TYPE_CLIPRECT:
            return "TYPE_CLIPRECT";
        case TYPE_CLIPRRECT:
            return "TYPE_CLIPRRECT";
        case TYPE_CLIPREGION:
            return "TYPE_CLIPREGION";
        case TYPE_CLIPSHADER:
            return "TYPE_CLIPSHADER";
        case TYPE_RESETCLIP:
            return "TYPE_RESETCLIP";
        case TYPE_DRAWPAINT:
            return "TYPE_DRAWPAINT";
        case TYPE_DRAWBEHIND:
            return "TYPE_DRAWBEHIND";
        case TYPE_DRAWPATH:
            return "TYPE_DRAWPATH";
        case TYPE_DRAWRECT:
            return "TYPE_DRAWRECT";
        case TYPE_DRAWREGION:
            return "TYPE_DRAWREGION";
        case TYPE_DRAWOVAL:
            return "TYPE_DRAWOVAL";
        case TYPE_DRAWARC:
            return "TYPE_DRAWARC";
        case TYPE_DRAWRRECT:
            return "TYPE_DRAWRRECT";
        case TYPE_DRAWDRRECT:
            return "TYPE_DRAWDRRECT";
        case TYPE_DRAWANNOTATION:
            return "TYPE_DRAWANNOTATION";
        case TYPE_DRAWDRAWABLE:
            return "TYPE_DRAWDRAWABLE";
        case TYPE_DRAWPICTURE:
            return "TYPE_DRAWPICTURE";
        case TYPE_DRAWIMAGE:
            return "TYPE_DRAWIMAGE";
        case TYPE_DRAWIMAGERECT:
            return "TYPE_DRAWIMAGERECT";
        case TYPE_DRAWIMAGELATTICE:
            return "TYPE_DRAWIMAGELATTICE";
        case TYPE_DRAWTEXTBLOB:
            return "TYPE_DRAWTEXTBLOB";
        case TYPE_DRAWPATCH:
            return "TYPE_DRAWPATCH";
        case TYPE_DRAWPOINTS:
            return "TYPE_DRAWPOINTS";
        case TYPE_DRAWVERTICES:
            return "TYPE_DRAWVERTICES";
        case TYPE_DRAWATLAS:
            return "TYPE_DRAWATLAS";
        case TYPE_DRAWSHADOWREC:
            return "TYPE_DRAWSHADOWREC";
        case TYPE_DRAWVECTORDRAWABLE:
            return "TYPE_DRAWVECTORDRAWABLE";
        case TYPE_DRAWRIPPLEDRAWABLE:
            return "TYPE_DRAWRIPPLEDRAWABLE";
        case TYPE_DRAWWEBVIEW:
            return "TYPE_DRAWWEBVIEW";
        case TYPE_DRAWSKMESH:
            return "TYPE_DRAWSKMESH";
        case TYPE_DRAWMESH:
            return "TYPE_DRAWMESH";
        case TYPE_DRAWPROXYSURFACECONTROL:
            return "TYPE_DRAWPROXYSURFACECONTROL";
        case TYPE_BEGINRENDERTARGET:
            return "TYPE_BEGINRENDERTARGET";
        case TYPE_ENDRENDERTARGET:
            return "TYPE_ENDRENDERTARGET";
        default:
            return "Unknown op type " + std::to_string(type);
    }
}

std::string skmatrixToString(const SkM44& matrix) {
    return std::to_string(matrix.rc(0, 0)) + std::string(" ") + std::to_string(matrix.rc(0, 1)) +
            std::string(" ") + std::to_string(matrix.rc(0, 2)) + std::string(" ") +
            std::to_string(matrix.rc(0, 3)) + std::string(" ") + std::to_string(matrix.rc(1, 0)) +
            std::string(" ") + std::to_string(matrix.rc(1, 1)) + std::string(" ") +
            std::to_string(matrix.rc(1, 2)) + std::string(" ") + std::to_string(matrix.rc(1, 3)) +
            std::string(" ") + std::to_string(matrix.rc(2, 0)) + std::string(" ") +
            std::to_string(matrix.rc(2, 1)) + std::string(" ") + std::to_string(matrix.rc(2, 2)) +
            std::string(" ") + std::to_string(matrix.rc(2, 3)) + std::string(" ") +
            std::to_string(matrix.rc(3, 0)) + std::string(" ") + std::to_string(matrix.rc(3, 1)) +
            std::string(" ") + std::to_string(matrix.rc(3, 2)) + std::string(" ") +
            std::to_string(matrix.rc(3, 3));
}

std::string rectToString(const SkRect& rect) {
    return std::to_string(rect.fLeft) + std::string(std::string(" ")) + std::to_string(rect.fTop) +
            std::string(std::string(" ")) + std::to_string(rect.fRight) +
            std::string(std::string(" ")) + std::to_string(rect.fBottom);
}

} // namespace android
