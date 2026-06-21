package android.gui;

/**
 * Composition filter flags describe the types or behaviors of a layer. SurfaceFlinger can filter
 * out layers with these flags to apply specific logic to particular groups of layers.
 *
 * @hide
 */
@Backing(type="int")
enum CompositionFilterFlag {
    /**
     * Indicates that the layer is a mouse cursor.
     */
    FLAG_MOUSE_CURSOR = 1,
    /**
     * Indicates that the layer should be treated as a screenshot UI element.
     */
    FLAG_SCREENSHOT_UI = 1 << 1,
    /**
     * Indicates that the layer should be treated as a status bar.
     */
    FLAG_STATUS_BAR = 1 << 2,
    /**
     * Indicates that the layer should be treated as an input method editor (IME).
     */
    FLAG_IME = 1 << 3,
}
