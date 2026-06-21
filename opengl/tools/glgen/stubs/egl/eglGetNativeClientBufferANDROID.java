    /**
     * Obtains an {@code EGLClientBuffer} from a {@link HardwareBuffer}.
     *
     * <p>The returned {@code EGLClientBuffer} is only valid as long as the {@code buffer}
     * instance is alive. Closing the {@code buffer} will invalidate the
     * {@code EGLClientBuffer}.</p>
     *
     * <p>No additional cleanup of the {@code EGLClientBuffer} is required beyond
     * closing the {@link HardwareBuffer}.</p>
     *
     * @param buffer A {@link HardwareBuffer} for which to obtain an EGLClientBuffer.
     * @return A native pointer to the EGLClientBuffer, or {@code 0} if the operation fails.
     *         The EGL error {@code EGL_BAD_PARAMETER} is generated if {@code buffer}
     *         is invalid or has been closed. To determine the error state, use
     *         {@link android.opengl.EGL14#eglGetError()}.
     *
     * @see <a href="https://www.khronos.org/registry/EGL/extensions/ANDROID/EGL_ANDROID_get_native_client_buffer.txt">
     *     EGL_ANDROID_get_native_client_buffer</a>
     */
    @FlaggedApi(Flags.FLAG_EGL_GET_NATIVE_CLIENT_BUFFER)
    public static native long eglGetNativeClientBufferANDROID(@NonNull HardwareBuffer buffer);

