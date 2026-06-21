    // C function EGLBoolean eglInitialize ( EGLDisplay dpy, EGLint *major, EGLint *minor )

    public static native boolean eglInitialize(
        EGLDisplay dpy,
        int[] major,
        int majorOffset,
        int[] minor,
        int minorOffset
    );

