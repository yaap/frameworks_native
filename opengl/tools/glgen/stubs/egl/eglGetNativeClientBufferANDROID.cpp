/* EGLClientBuffer eglGetNativeClientBufferANDROID ( struct AHardwareBuffer const *buffer ) */
static jlong android_eglGetNativeClientBufferANDROID(JNIEnv* _env, jobject, jobject buffer) {
    auto ahb = android::android_hardware_HardwareBuffer_getNativeHardwareBuffer(_env, buffer);
    return reinterpret_cast<jlong>(eglGetNativeClientBufferANDROID(ahb));
}

