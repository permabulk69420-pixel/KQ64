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

    public static Configuration load(Context context) {
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
        Map<String, ?> values = preferences.getAll();
        final boolean enabled = preferences.getBoolean("enabled", true);
        QuestVrDiagnostics.info("QuestVrSettings", "Loaded preferences name=" +
                PREFERENCES_NAME + " containsEnabled=" + preferences.contains("enabled") +
                " rawEnabled=" + values.get("enabled") + " vrEnabled=" + enabled +
                " entryCount=" + values.size());
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
                getFloat(values, "hud_depth_meters", 2.0f),
                getFloat(values, "hud_scale", 1.0f),
                preferences.getString("hud_mode", "monoscopic_overlay"),
                getFloat(values, "culling_expansion", 1.25f),
                preferences.getBoolean("use_openxr_fov", true),
                preferences.getBoolean("mario_kart_profile_enabled", true),
                getFloat(values, "mario_kart_camera_offset_y_meters", -0.20f),
                getFloat(values, "mario_kart_camera_offset_z_meters", -0.75f),
                getFloat(values, "stereo_source_width_scale", 1.0f),
                getInt(values, "max_stereo_source_width", 4096),
                preferences.getBoolean("touch_controller_enabled", true),
                preferences.getBoolean("debug_logging", true));
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
        public final float hudDepthMeters;
        public final float hudScale;
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

        private Configuration(boolean enabled, boolean stereoEnabled, boolean swapEyes, float ipdMeters,
                float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
                float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
                float cameraOffsetZMeters, float nearPlane, float farPlane, float hudDepthMeters,
                float hudScale, String hudMode, float cullingExpansion,
                boolean useOpenXrFov, boolean marioKartProfileEnabled,
                float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
                float stereoSourceWidthScale, int maxStereoSourceWidth,
                boolean touchControllerEnabled, boolean debugLogging) {
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
            this.hudDepthMeters = hudDepthMeters;
            this.hudScale = hudScale;
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
        }
    }
}
