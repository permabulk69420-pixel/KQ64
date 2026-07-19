package paulscode.android.mupen64plusae.questvr;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Map;

/**
 * Developer-facing Quest VR settings. The initial prototype deliberately keeps
 * these separate from normal emulator preferences so VR can remain reversible.
 */
public final class QuestVrSettings {
    public static final String PREFERENCES_NAME = "quest_vr";
    public static final int SAFE_MAX_SOURCE_WIDTH = 2688;
    public static final int SAFE_MAX_SOURCE_HEIGHT = 1440;
    public static final int SAFE_MAX_SOURCE_PIXELS = 3_000_000;

    private static final float MIN_SCREEN_SCALE = 0.35f;
    private static final float MAX_SCREEN_SCALE = 2.0f;
    private static final float MIN_SCREEN_DISTANCE_METERS = 0.75f;
    private static final float MAX_SCREEN_DISTANCE_METERS = 6.0f;

    private QuestVrSettings() {
    }

    private static float getFloat(Map<String, ?> values, String key, float defaultValue) {
        Object value = values.get(key);
        if (value instanceof Number) {
            return ((Number) value).floatValue();
        }
        if (value instanceof String) {
            try {
                return Float.parseFloat((String) value);
            } catch (NumberFormatException ignored) {
                // Keep the documented default for malformed developer input.
            }
        }
        return defaultValue;
    }

    private static int getInt(Map<String, ?> values, String key, int defaultValue) {
        Object value = values.get(key);
        if (value instanceof Number) {
            return ((Number) value).intValue();
        }
        if (value instanceof String) {
            try {
                return Integer.parseInt((String) value);
            } catch (NumberFormatException ignored) {
                // Keep the documented default for malformed developer input.
            }
        }
        return defaultValue;
    }

    private static float clampFinite(float value, float minimum, float maximum,
            float defaultValue) {
        if (!Float.isFinite(value)) {
            return defaultValue;
        }
        return Math.max(minimum, Math.min(maximum, value));
    }

    private static int clamp(int value, int minimum, int maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    public static Configuration load(Context context) {
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
        Map<String, ?> values = preferences.getAll();
        final boolean enabled = preferences.getBoolean("enabled", true);
        final float requestedScreenDistance = getFloat(values, "screen_distance_meters",
                getFloat(values, "hud_depth_meters", 2.0f));
        final float screenDistance = clampFinite(requestedScreenDistance,
                MIN_SCREEN_DISTANCE_METERS, MAX_SCREEN_DISTANCE_METERS, 2.0f);
        final float requestedScreenScale = getFloat(values, "screen_scale",
                getFloat(values, "hud_scale", 1.0f));
        final float screenScale = clampFinite(requestedScreenScale,
                MIN_SCREEN_SCALE, MAX_SCREEN_SCALE, 1.0f);
        final float requestedSourceWidthScale =
                getFloat(values, "stereo_source_width_scale", 1.0f);
        final float sourceWidthScale = clampFinite(requestedSourceWidthScale,
                1.0f, 2.0f, 1.0f);
        final int requestedMaxSourceWidth =
                getInt(values, "max_stereo_source_width", SAFE_MAX_SOURCE_WIDTH);
        final int maxSourceWidth = clamp(requestedMaxSourceWidth, 640,
                SAFE_MAX_SOURCE_WIDTH);
        QuestVrDiagnostics.info("QuestVrSettings", "Loaded preferences name=" +
                PREFERENCES_NAME + " containsEnabled=" + preferences.contains("enabled") +
                " rawEnabled=" + values.get("enabled") + " vrEnabled=" + enabled +
                " entryCount=" + values.size() +
                " screenDistance=" + requestedScreenDistance + "->" + screenDistance +
                " screenScale=" + requestedScreenScale + "->" + screenScale +
                " sourceWidthScale=" + requestedSourceWidthScale + "->" +
                sourceWidthScale + " sourceWidthCap=" + requestedMaxSourceWidth + "->" +
                maxSourceWidth + " proofLayers=" +
                preferences.getBoolean("startup_proof_layers", false));
        return new Configuration(
                enabled,
                preferences.getBoolean("stereo_enabled", true),
                preferences.getBoolean("swap_eyes", false),
                getFloat(values, "ipd_meters", 0.064f),
                getFloat(values, "world_units_per_meter", 64.0f),
                getFloat(values, "rotation_strength", 1.0f),
                preferences.getBoolean("position_enabled", false),
                getFloat(values, "max_translation_meters", 0.15f),
                getFloat(values, "camera_offset_x_meters", 0.0f),
                getFloat(values, "camera_offset_y_meters", 0.0f),
                getFloat(values, "camera_offset_z_meters", 0.0f),
                getFloat(values, "near_plane", 0.1f),
                getFloat(values, "far_plane", 10000.0f),
                screenDistance,
                screenScale,
                preferences.getString("hud_mode", "monoscopic_overlay"),
                getFloat(values, "culling_expansion", 1.25f),
                preferences.getBoolean("use_openxr_fov", true),
                preferences.getBoolean("mario_kart_profile_enabled", true),
                getFloat(values, "mario_kart_camera_offset_y_meters", -0.20f),
                getFloat(values, "mario_kart_camera_offset_z_meters", -0.75f),
                sourceWidthScale,
                maxSourceWidth,
                preferences.getBoolean("touch_controller_enabled", true),
                preferences.getBoolean("debug_logging", true),
                preferences.getBoolean("startup_proof_layers", false));
    }

    /**
     * Resolve the actual GLideN64/SurfaceTexture producer size before the core starts.
     *
     * The stereo plug-in divides the full producer width between both eyes. The width scale is
     * therefore deliberately independent of presentation size: 1 uses half horizontal source
     * resolution per eye, while 2 preserves the flat render's horizontal resolution per eye.
     * Oversized EGL pbuffers have failed silently on Quest around 2880 pixels wide, so all limits
     * are applied proportionally and preserve the requested source-buffer aspect ratio.
     */
    public static SourceRenderSize resolveStereoSourceSize(int baseWidth, int baseHeight,
            Configuration configuration) {
        final int safeBaseWidth = Math.max(2, baseWidth);
        final int safeBaseHeight = Math.max(2, baseHeight);
        final int requestedWidth = evenDimension(
                safeBaseWidth * (double) configuration.stereoSourceWidthScale);
        final int requestedHeight = evenDimension(safeBaseHeight);

        double downscale = 1.0;
        downscale = Math.min(downscale,
                configuration.maxStereoSourceWidth / (double) requestedWidth);
        downscale = Math.min(downscale,
                SAFE_MAX_SOURCE_HEIGHT / (double) requestedHeight);
        final double requestedPixels = (double) requestedWidth * requestedHeight;
        if (requestedPixels > SAFE_MAX_SOURCE_PIXELS) {
            downscale = Math.min(downscale,
                    Math.sqrt(SAFE_MAX_SOURCE_PIXELS / requestedPixels));
        }
        downscale = Math.max(0.0, Math.min(1.0, downscale));

        final int actualWidth = evenDimension(requestedWidth * downscale);
        final int actualHeight = evenDimension(requestedHeight * downscale);
        return new SourceRenderSize(safeBaseWidth, safeBaseHeight, requestedWidth,
                requestedHeight, actualWidth, actualHeight, downscale,
                safeBaseWidth / (float) safeBaseHeight,
                requestedWidth != actualWidth || requestedHeight != actualHeight);
    }

    private static int evenDimension(double value) {
        final double bounded = Math.max(2.0, Math.min(Integer.MAX_VALUE - 1.0, value));
        return Math.max(2, ((int) Math.floor(bounded)) & ~1);
    }

    public static final class SourceRenderSize {
        public final int baseWidth;
        public final int baseHeight;
        public final int requestedWidth;
        public final int requestedHeight;
        public final int actualWidth;
        public final int actualHeight;
        public final double downscale;
        public final float contentAspect;
        public final boolean clamped;

        private SourceRenderSize(int baseWidth, int baseHeight, int requestedWidth,
                int requestedHeight, int actualWidth, int actualHeight, double downscale,
                float contentAspect, boolean clamped) {
            this.baseWidth = baseWidth;
            this.baseHeight = baseHeight;
            this.requestedWidth = requestedWidth;
            this.requestedHeight = requestedHeight;
            this.actualWidth = actualWidth;
            this.actualHeight = actualHeight;
            this.downscale = downscale;
            this.contentAspect = contentAspect;
            this.clamped = clamped;
        }
    }

    public static final class Configuration {
        public final boolean enabled;
        public final boolean stereoEnabled;
        public final boolean swapEyes;
        public final float ipdMeters;
        public final float worldUnitsPerMeter;
        public final float rotationStrength;
        public final boolean positionEnabled;
        public final float maxTranslationMeters;
        public final float cameraOffsetXMeters;
        public final float cameraOffsetYMeters;
        public final float cameraOffsetZMeters;
        public final float nearPlane;
        public final float farPlane;
        public final float screenDistanceMeters;
        public final float screenScale;
        public final String hudMode;
        public final float cullingExpansion;
        public final boolean useOpenXrFov;
        public final boolean marioKartProfileEnabled;
        public final float marioKartCameraOffsetYMeters;
        public final float marioKartCameraOffsetZMeters;
        public final float stereoSourceWidthScale;
        public final int maxStereoSourceWidth;
        public final boolean touchControllerEnabled;
        public final boolean debugLogging;
        public final boolean startupProofLayers;

        private Configuration(boolean enabled, boolean stereoEnabled, boolean swapEyes, float ipdMeters,
                float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
                float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
                float cameraOffsetZMeters, float nearPlane, float farPlane,
                float screenDistanceMeters, float screenScale, String hudMode,
                float cullingExpansion,
                boolean useOpenXrFov, boolean marioKartProfileEnabled,
                float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
                float stereoSourceWidthScale, int maxStereoSourceWidth,
                boolean touchControllerEnabled, boolean debugLogging,
                boolean startupProofLayers) {
            this.enabled = enabled;
            this.stereoEnabled = stereoEnabled;
            this.swapEyes = swapEyes;
            this.ipdMeters = ipdMeters;
            this.worldUnitsPerMeter = worldUnitsPerMeter;
            this.rotationStrength = rotationStrength;
            this.positionEnabled = positionEnabled;
            this.maxTranslationMeters = maxTranslationMeters;
            this.cameraOffsetXMeters = cameraOffsetXMeters;
            this.cameraOffsetYMeters = cameraOffsetYMeters;
            this.cameraOffsetZMeters = cameraOffsetZMeters;
            this.nearPlane = nearPlane;
            this.farPlane = farPlane;
            this.screenDistanceMeters = screenDistanceMeters;
            this.screenScale = screenScale;
            this.hudMode = hudMode;
            this.cullingExpansion = cullingExpansion;
            this.useOpenXrFov = useOpenXrFov;
            this.marioKartProfileEnabled = marioKartProfileEnabled;
            this.marioKartCameraOffsetYMeters = marioKartCameraOffsetYMeters;
            this.marioKartCameraOffsetZMeters = marioKartCameraOffsetZMeters;
            this.stereoSourceWidthScale = stereoSourceWidthScale;
            this.maxStereoSourceWidth = maxStereoSourceWidth;
            this.touchControllerEnabled = touchControllerEnabled;
            this.debugLogging = debugLogging;
            this.startupProofLayers = startupProofLayers;
        }
    }
}
