package paulscode.android.mupen64plusae.questvr;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Developer-facing Quest VR settings. The initial prototype deliberately keeps
 * these separate from normal emulator preferences so VR can remain reversible.
 */
public final class QuestVrSettings {
    public static final String PREFERENCES_NAME = "quest_vr";

    private QuestVrSettings() {
    }

    public static Configuration load(Context context) {
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
        return new Configuration(
                preferences.getBoolean("enabled", true),
                preferences.getBoolean("stereo_enabled", true),
                preferences.getFloat("ipd_meters", 0.064f),
                preferences.getFloat("world_units_per_meter", 64.0f),
                preferences.getFloat("rotation_strength", 1.0f),
                preferences.getBoolean("position_enabled", false),
                preferences.getFloat("max_translation_meters", 0.15f),
                preferences.getFloat("camera_offset_x_meters", 0.0f),
                preferences.getFloat("camera_offset_y_meters", 0.0f),
                preferences.getFloat("camera_offset_z_meters", 0.0f),
                preferences.getFloat("near_plane", 0.1f),
                preferences.getFloat("far_plane", 10000.0f),
                preferences.getFloat("hud_depth_meters", 2.0f),
                preferences.getFloat("hud_scale", 1.0f),
                preferences.getString("hud_mode", "monoscopic_overlay"),
                preferences.getFloat("culling_expansion", 1.25f),
                preferences.getBoolean("use_openxr_fov", true),
                preferences.getBoolean("mario_kart_profile_enabled", true),
                preferences.getFloat("mario_kart_camera_offset_y_meters", -0.20f),
                preferences.getFloat("mario_kart_camera_offset_z_meters", -0.75f),
                preferences.getBoolean("touch_controller_enabled", true),
                preferences.getBoolean("debug_logging", false));
    }

    public static final class Configuration {
        public final boolean enabled;
        public final boolean stereoEnabled;
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
        public final boolean touchControllerEnabled;
        public final boolean debugLogging;

        private Configuration(boolean enabled, boolean stereoEnabled, float ipdMeters,
                float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
                float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
                float cameraOffsetZMeters, float nearPlane, float farPlane, float hudDepthMeters,
                float hudScale, String hudMode, float cullingExpansion,
                boolean useOpenXrFov, boolean marioKartProfileEnabled,
                float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
                boolean touchControllerEnabled, boolean debugLogging) {
            this.enabled = enabled;
            this.stereoEnabled = stereoEnabled;
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
            this.touchControllerEnabled = touchControllerEnabled;
            this.debugLogging = debugLogging;
        }
    }
}
