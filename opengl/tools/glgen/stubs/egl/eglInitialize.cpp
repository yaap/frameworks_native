/* EGLBoolean eglInitialize ( EGLDisplay dpy, EGLint *major, EGLint *minor ) */
static jboolean
android_eglInitialize
  (JNIEnv *_env, jobject _this, jobject dpy, jintArray major_ref, jint majorOffset, jintArray minor_ref, jint minorOffset) {
    EGLDisplay dpy_native = (EGLDisplay) fromEGLHandle(_env, egldisplayGetHandleID, dpy);
    EGLint majorVersion;
    EGLint minorVersion;

    if (major_ref) {
        if (majorOffset < 0 || majorOffset >= _env->GetArrayLength(major_ref)) {
            jniThrowException(_env, "java/lang/IllegalArgumentException",
                    "majorOffset outside array");
            return JNI_FALSE;
        }
    }

    if (minor_ref) {
        if (minorOffset < 0 || minorOffset >= _env->GetArrayLength(minor_ref)) {
            jniThrowException(_env, "java/lang/IllegalArgumentException",
                    "minorOffset outside array");
            return JNI_FALSE;
        }
    }

    if (EGL_TRUE != eglInitialize(dpy_native, &majorVersion, &minorVersion)) {
        return JNI_FALSE;
    }

    if (major_ref) {
        _env->SetIntArrayRegion(major_ref, majorOffset, 1, &majorVersion);
    }
    if (minor_ref) {
        _env->SetIntArrayRegion(minor_ref, minorOffset, 1, &minorVersion);
    }

    return JNI_TRUE;
}

