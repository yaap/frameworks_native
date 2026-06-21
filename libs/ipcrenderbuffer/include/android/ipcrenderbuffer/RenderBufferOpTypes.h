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

#pragma once

namespace android {

enum RenderBufferOpType {
    TYPE_SAVE = 0,
    TYPE_RESTORE = 1,
    TYPE_SAVELAYER = 2,
    TYPE_SAVEBEHIND = 3,
    TYPE_CONCAT = 4,
    TYPE_SETMATRIX = 5,
    TYPE_SCALE = 6,
    TYPE_TRANSLATE = 7,
    TYPE_CLIPPATH = 8,
    TYPE_CLIPRECT = 9,
    TYPE_CLIPRRECT = 10,
    TYPE_CLIPREGION = 11,
    TYPE_CLIPSHADER = 12,
    TYPE_RESETCLIP = 13,
    TYPE_DRAWPAINT = 14,
    TYPE_DRAWBEHIND = 15,
    TYPE_DRAWPATH = 16,
    TYPE_DRAWRECT = 17,
    TYPE_DRAWREGION = 18,
    TYPE_DRAWOVAL = 19,
    TYPE_DRAWARC = 20,
    TYPE_DRAWRRECT = 21,
    TYPE_DRAWDRRECT = 22,
    TYPE_DRAWANNOTATION = 23,
    TYPE_DRAWDRAWABLE = 24,
    TYPE_DRAWPICTURE = 25,
    TYPE_DRAWIMAGE = 26,
    TYPE_DRAWIMAGERECT = 27,
    TYPE_DRAWIMAGELATTICE = 28,
    TYPE_DRAWTEXTBLOB = 29,
    TYPE_DRAWPATCH = 30,
    TYPE_DRAWPOINTS = 31,
    TYPE_DRAWVERTICES = 32,
    TYPE_DRAWATLAS = 33,
    TYPE_DRAWSHADOWREC = 34,
    TYPE_DRAWVECTORDRAWABLE = 35,
    TYPE_DRAWRIPPLEDRAWABLE = 36,
    TYPE_DRAWWEBVIEW = 37,
    TYPE_DRAWSKMESH = 38,
    TYPE_DRAWMESH = 39,
    TYPE_DRAWPROXYSURFACECONTROL = 40,
    TYPE_BEGINRENDERTARGET = 41,
    TYPE_ENDRENDERTARGET = 42,
    TYPE_UPLOADBITMAP = 43,
    TYPE_FREEBITMAP = 44,
    TYPE_UPLOADTYPEFACE = 45,
};

} // namespace android
