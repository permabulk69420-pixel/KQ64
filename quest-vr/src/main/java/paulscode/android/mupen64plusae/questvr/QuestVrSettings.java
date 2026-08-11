package paulscode.android.mupen64plusae.questvr;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import java.util.Locale;
import java.util.Map;

/**
 * Developer-facing Quest VR settings. The initial prototype deliberately keeps
 * these separate from normal emulator preferences so VR can remain reversible.
 */
public final class QuestVrSettings {
    public static final String PREFERENCES_NAME = "quest_vr";
    public static final String PRESENTATION_IMMERSIVE_PROJECTION = "immersive_projection";
    public static final String PRESENTATION_CINEMA_SCREEN = "cinema_screen";
    public static final String EXTRA_PRESENTATION_MODE =
            "paulscode.android.mupen64plusae.questvr.PRESENTATION_MODE";
    public static final int PRESENTATION_MODE_IMMERSIVE_PROJECTION = 0;
    public static final int PRESENTATION_MODE_CINEMA_SCREEN = 1;
    public static final int SAFE_MAX_SOURCE_WIDTH = 2688;
    public static final int SAFE_MAX_SOURCE_HEIGHT = 1440;
    public static final int SAFE_MAX_SOURCE_PIXELS = 3_000_000;

    /**
     * Shipping default for {@code max_stereo_source_width}, and the ceilings a larger value opts
     * into.
     *
     * The safe limits above used to be the default, which pinned every render-resolution preset to
     * the same 1344x1008 per eye: the downscale is uniform, so a width clamp took the same
     * proportion off the height and the larger presets cost memory without adding a pixel. Because
     * one eye of a 2688 wide pair is exactly 1344, the preset that fits was also the only preset
     * that did anything.
     *
     * The default now clears that plateau, so a Quest install renders 1920x1440 per eye without
     * anyone having to discover a number to type. Values above the safe width lift the height and
     * megapixel budgets with it, since raising width alone leaves the uniform downscale pinned by
     * whichever other budget clamps first.
     *
     * A larger producer is the thing to suspect first if a headset shows a black or missing image,
     * because an oversized EGL pbuffer can fail without reporting an error. The resolved size is
     * reported in the producer-resolution diagnostics, and lowering this preference is the way
     * back.
     */
    public static final int DEFAULT_MAX_SOURCE_WIDTH = 3840;
    public static final int MAX_SOURCE_WIDTH_OVERRIDE = 4096;
    public static final int MAX_SOURCE_HEIGHT_OVERRIDE = 2048;
    public static final int MAX_SOURCE_PIXELS_OVERRIDE = 8_400_000;

    private static final float MIN_SCREEN_SCALE = 0.35f;
    private static final float MAX_SCREEN_SCALE = 2.0f;
    private static final float MIN_SCREEN_DISTANCE_METERS = 0.75f;
    private static final float MAX_SCREEN_DISTANCE_METERS = 6.0f;
    private static final float MIN_IMMERSIVE_SCALE = 0.50f;
    private static final float MAX_IMMERSIVE_SCALE = 1.00f;
    private static final String SOURCE_WIDTH_MULTIPLIER_V2 =
            "stereo_source_width_multiplier_v2";
    private static final String QUEST_GRAPHICS_DEFAULTS_VERSION_KEY =
            "quest_graphics_defaults_version";
    private static final int QUEST_GRAPHICS_DEFAULTS_VERSION = 2;
    private static final String QUEST_RENDER_RESOLUTION = "1440";
    private static final String QUEST_EMULATION_PROFILE = "GlideN64-Very-Accurate";

    private QuestVrSettings() {
    }

    private static boolean isQuestHardware(Context context) {
        final String manufacturer = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER;
        final String model = Build.MODEL == null ? "" : Build.MODEL;
        return context.getPackageManager().hasSystemFeature("android.hardware.vr.headtracking") ||
                "oculus".equals(manufacturer.toLowerCase(Locale.US)) ||
                "meta".equals(manufacturer.toLowerCase(Locale.US)) ||
                model.toLowerCase(Locale.US).contains("quest");
    }

    /**
     * Upgrade Quest installs once to the hardware-tested quality baseline. These are persistent
     * defaults rather than per-launch overrides: after this version marker is stored, Advanced
     * settings remain user-controlled and are not rewritten on every game launch.
     */
    private static void ensureQuestGraphicsDefaults(Context context,
            SharedPreferences questPreferences, SharedPreferences globalPreferences) {
        if (!isQuestHardware(context) || questPreferences.getInt(
                QUEST_GRAPHICS_DEFAULTS_VERSION_KEY, 0) >= QUEST_GRAPHICS_DEFAULTS_VERSION) {
            return;
        }

        final boolean globalSaved = globalPreferences.edit()
                .putString("displayResolution", QUEST_RENDER_RESOLUTION)
                .putString("emulationProfileDefault", QUEST_EMULATION_PROFILE)
                .commit();
        if (!globalSaved) {
            QuestVrDiagnostics.warn("QuestVrSettings",
                    "Unable to persist Quest graphics defaults; leaving upgrade unmarked");
            return;
        }

        final boolean questSaved = questPreferences.edit()
                .putBoolean("enabled", true)
                .putBoolean("stereo_enabled", true)
                .putString(SOURCE_WIDTH_MULTIPLIER_V2, "2.0")
                .putString("max_stereo_source_width", Integer.toString(DEFAULT_MAX_SOURCE_WIDTH))
                .putInt(QUEST_GRAPHICS_DEFAULTS_VERSION_KEY, QUEST_GRAPHICS_DEFAULTS_VERSION)
                .commit();
        if (questSaved) {
            QuestVrDiagnostics.info("QuestVrSettings",
                    "Applied persistent Quest graphics preset version=" +
                            QUEST_GRAPHICS_DEFAULTS_VERSION + " resolution=" +
                            QUEST_RENDER_RESOLUTION + " profile=" + QUEST_EMULATION_PROFILE +
                            " stereoSource=2.0 maxWidth=" + DEFAULT_MAX_SOURCE_WIDTH);
        } else {
            QuestVrDiagnostics.warn("QuestVrSettings",
                    "Global Quest graphics defaults saved, but VR defaults marker failed");
        }
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
        final SharedPreferences globalPreferences = context.getSharedPreferences(
                context.getPackageName() + "_preferences", Context.MODE_PRIVATE);
        ensureQuestGraphicsDefaults(context, preferences, globalPreferences);
        Map<String, ?> values = preferences.getAll();
        final boolean preferenceEnabled = preferences.getBoolean("enabled", true);
        final float requestedScreenDistance = getFloat(values, "screen_distance_meters",
                getFloat(values, "hud_depth_meters", 2.0f));
        final float screenDistance = clampFinite(requestedScreenDistance,
                MIN_SCREEN_DISTANCE_METERS, MAX_SCREEN_DISTANCE_METERS, 2.0f);
        final float requestedScreenScale = getFloat(values, "screen_scale",
                getFloat(values, "hud_scale", 1.0f));
        final float screenScale = clampFinite(requestedScreenScale,
                MIN_SCREEN_SCALE, MAX_SCREEN_SCALE, 1.0f);
        // Before this explicit setting existed, the only user-facing "Immersive mode" switch was
        // Android's display/chrome preference. Honor its enabled state as a one-way fallback for
        // existing installs, while the new Quest-specific setting takes precedence as soon as it
        // exists. The default shared-preference filename is defined by PreferenceManager as the
        // package name plus "_preferences"; avoiding an androidx dependency keeps this module thin.
        final boolean legacyAndroidImmersive = globalPreferences.getBoolean(
                "displayImmersiveMode_v2", true);
        final String preferencePresentation = preferences.getString("presentation_mode",
                legacyAndroidImmersive ? PRESENTATION_IMMERSIVE_PROJECTION
                        : PRESENTATION_CINEMA_SCREEN);
        final String launchPresentation = context instanceof Activity
                ? ((Activity) context).getIntent().getStringExtra(EXTRA_PRESENTATION_MODE) : null;
        final boolean launchPresentationValid =
                PRESENTATION_CINEMA_SCREEN.equals(launchPresentation) ||
                PRESENTATION_IMMERSIVE_PROJECTION.equals(launchPresentation);
        // A valid game-launch mode is an explicit request to start the Quest VR path. Treat it as
        // authoritative for this process launch so stale cross-process SharedPreferences cannot
        // disable VR or stereo between the launcher and GameActivity.
        final boolean enabled = launchPresentationValid || preferenceEnabled;
        final boolean stereoEnabled = launchPresentationValid ||
                preferences.getBoolean("stereo_enabled", true);
        final String requestedPresentation = launchPresentationValid
                ? launchPresentation : preferencePresentation;
        final int presentationMode = PRESENTATION_CINEMA_SCREEN.equals(requestedPresentation)
                ? PRESENTATION_MODE_CINEMA_SCREEN
                : PRESENTATION_MODE_IMMERSIVE_PROJECTION;
        final String presentationName = presentationMode == PRESENTATION_MODE_CINEMA_SCREEN
                ? PRESENTATION_CINEMA_SCREEN : PRESENTATION_IMMERSIVE_PROJECTION;
        final float requestedImmersiveScale = getFloat(values, "immersive_view_scale", 1.0f);
        final float immersiveScale = clampFinite(requestedImmersiveScale,
                MIN_IMMERSIVE_SCALE, MAX_IMMERSIVE_SCALE, 1.0f);
        // V2 changes the old width-only scale into an aspect-preserving stereo-resolution
        // multiplier. A new key intentionally gives existing installs the corrected 2.0 default
        // instead of carrying forward the prototype's malformed 1.0 side-by-side buffer.
        final float legacySourceWidthScale =
                getFloat(values, "stereo_source_width_scale", Float.NaN);
        final float requestedSourceWidthMultiplier =
                getFloat(values, SOURCE_WIDTH_MULTIPLIER_V2, 2.0f);
        final float sourceWidthMultiplier = clampFinite(requestedSourceWidthMultiplier,
                0.5f, 2.0f, 2.0f);
        final int requestedMaxSourceWidth =
                getInt(values, "max_stereo_source_width", DEFAULT_MAX_SOURCE_WIDTH);
        final int maxSourceWidth = clamp(requestedMaxSourceWidth, 640,
                MAX_SOURCE_WIDTH_OVERRIDE);
        QuestVrDiagnostics.info("QuestVrSettings", "Loaded preferences name=" +
                PREFERENCES_NAME + " containsEnabled=" + preferences.contains("enabled") +
                " rawEnabled=" + values.get("enabled") +
                " preferenceEnabled=" + preferenceEnabled + " vrEnabled=" + enabled +
                " stereoEnabled=" + stereoEnabled +
                " entryCount=" + values.size() +
                " screenDistance=" + requestedScreenDistance + "->" + screenDistance +
                " screenScale=" + requestedScreenScale + "->" + screenScale +
                " presentation=" + requestedPresentation + "->" + presentationName +
                " launchOverride=" + launchPresentationValid +
                " preferencePresentation=" + preferencePresentation +
                " explicitPresentation=" + preferences.contains("presentation_mode") +
                " legacyAndroidImmersive=" + legacyAndroidImmersive +
                " immersiveScale=" + requestedImmersiveScale + "->" + immersiveScale +
                " sourceWidthMultiplierV2=" + requestedSourceWidthMultiplier + "->" +
                sourceWidthMultiplier + " legacyWidthScale=" + legacySourceWidthScale +
                " legacyIgnored=" + (!values.containsKey(SOURCE_WIDTH_MULTIPLIER_V2)) +
                " sourceWidthCap=" + requestedMaxSourceWidth + "->" +
                maxSourceWidth + " proofLayers=" +
                preferences.getBoolean("startup_proof_layers", false));
        return new Configuration(
                enabled,
                presentationMode,
                presentationName,
                immersiveScale,
                stereoEnabled,
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
                getFloat(values, "mario_kart_camera_offset_y_meters", 0.0f),
                getFloat(values, "mario_kart_camera_offset_z_meters", 0.0f),
                sourceWidthMultiplier,
                maxSourceWidth,
                preferences.getBoolean("touch_controller_enabled", true),
                preferences.getBoolean("debug_logging", true),
                preferences.getBoolean("startup_proof_layers", false));
    }

    /**
     * Stable mono configuration for the VR launcher. This is deliberately not persisted and does
     * not replace the user's selected game mode; it only presents the Canvas library on a
     * world-locked cinema quad.
     */
    public static Configuration loadLauncher(Context context) {
        final Configuration game = load(context);
        return new Configuration(
                true,
                PRESENTATION_MODE_CINEMA_SCREEN,
                PRESENTATION_CINEMA_SCREEN,
                1.0f,
                false,
                false,
                game.ipdMeters,
                game.worldUnitsPerMeter,
                0.0f,
                false,
                game.maxTranslationMeters,
                0.0f,
                0.0f,
                0.0f,
                game.nearPlane,
                game.farPlane,
                2.0f,
                1.0f,
                "monoscopic_overlay",
                game.cullingExpansion,
                game.useOpenXrFov,
                false,
                0.0f,
                0.0f,
                game.stereoSourceWidthMultiplier,
                game.maxStereoSourceWidth,
                true,
                game.debugLogging,
                false);
    }

    /**
     * Resolve the actual GLideN64/SurfaceTexture producer size before the core starts.
     *
     * The selected flat render size is the desired per-eye content size. At the default 2.0
     * multiplier the complete side-by-side request is twice the selected width and each eye keeps
     * the selected width and height. Lower multipliers reduce both axes together, preserving the
     * per-eye content aspect instead of creating tall, narrow eye images.
     * Oversized EGL pbuffers have failed silently on Quest around 2880 pixels wide, so all limits
     * are applied proportionally and preserve the requested source-buffer aspect ratio.
     */
    public static SourceRenderSize resolveStereoSourceSize(int baseWidth, int baseHeight,
            Configuration configuration) {
        final int safeBaseWidth = Math.max(2, baseWidth);
        final int safeBaseHeight = Math.max(2, baseHeight);
        final int requestedWidth = stereoWidthDimension(
                safeBaseWidth * (double) configuration.stereoSourceWidthMultiplier);
        final int requestedHeight = evenDimension(
                safeBaseHeight * (double) configuration.stereoSourceWidthMultiplier * 0.5);

        // Raising the width preference past the safe value lifts the height and pixel budgets
        // with it. Lifting the width alone would leave the uniform downscale below pinned by
        // whichever of the other two clamps first, so a taller preset would still collapse back
        // to the plateau it is trying to escape.
        final boolean overrideLimits =
                configuration.maxStereoSourceWidth > SAFE_MAX_SOURCE_WIDTH;
        final int maxSourceHeight =
                overrideLimits ? MAX_SOURCE_HEIGHT_OVERRIDE : SAFE_MAX_SOURCE_HEIGHT;
        final int maxSourcePixels =
                overrideLimits ? MAX_SOURCE_PIXELS_OVERRIDE : SAFE_MAX_SOURCE_PIXELS;

        final double widthDownscale = Math.min(1.0,
                configuration.maxStereoSourceWidth / (double) requestedWidth);
        final double heightDownscale = Math.min(1.0,
                maxSourceHeight / (double) requestedHeight);
        final double requestedPixels = (double) requestedWidth * requestedHeight;
        final double pixelDownscale = requestedPixels > maxSourcePixels
                ? Math.sqrt(maxSourcePixels / requestedPixels) : 1.0;
        double downscale = Math.min(widthDownscale,
                Math.min(heightDownscale, pixelDownscale));
        downscale = Math.max(0.0, Math.min(1.0, downscale));

        final int actualWidth = stereoWidthDimension(requestedWidth * downscale);
        final int actualHeight = evenDimension(requestedHeight * downscale);
        final int requestedPerEyeWidth = requestedWidth / 2;
        final int actualPerEyeWidth = actualWidth / 2;
        final double epsilon = 1.0e-9;
        return new SourceRenderSize(safeBaseWidth, safeBaseHeight, requestedWidth,
                requestedHeight, requestedPerEyeWidth, actualWidth, actualHeight,
                actualPerEyeWidth, downscale,
                actualPerEyeWidth / (float) actualHeight,
                requestedWidth != actualWidth || requestedHeight != actualHeight,
                widthDownscale < 1.0 && widthDownscale <= downscale + epsilon,
                heightDownscale < 1.0 && heightDownscale <= downscale + epsilon,
                pixelDownscale < 1.0 && pixelDownscale <= downscale + epsilon);
    }

    private static int evenDimension(double value) {
        final double bounded = Math.max(2.0, Math.min(Integer.MAX_VALUE - 1.0, value));
        return Math.max(2, ((int) Math.floor(bounded + 1.0e-6)) & ~1);
    }

    /** Complete SBS width divisible by four so each eye receives an even width. */
    private static int stereoWidthDimension(double value) {
        final double bounded = Math.max(4.0, Math.min(Integer.MAX_VALUE - 3.0, value));
        return Math.max(4, ((int) Math.floor(bounded + 1.0e-6)) & ~3);
    }

    public static final class SourceRenderSize {
        public final int baseWidth;
        public final int baseHeight;
        public final int requestedWidth;
        public final int requestedHeight;
        public final int requestedPerEyeWidth;
        public final int actualWidth;
        public final int actualHeight;
        public final int actualPerEyeWidth;
        public final double downscale;
        public final float contentAspect;
        public final boolean clamped;
        public final boolean widthLimited;
        public final boolean heightLimited;
        public final boolean pixelLimited;

        private SourceRenderSize(int baseWidth, int baseHeight, int requestedWidth,
                int requestedHeight, int requestedPerEyeWidth, int actualWidth,
                int actualHeight, int actualPerEyeWidth, double downscale,
                float contentAspect, boolean clamped, boolean widthLimited,
                boolean heightLimited, boolean pixelLimited) {
            this.baseWidth = baseWidth;
            this.baseHeight = baseHeight;
            this.requestedWidth = requestedWidth;
            this.requestedHeight = requestedHeight;
            this.requestedPerEyeWidth = requestedPerEyeWidth;
            this.actualWidth = actualWidth;
            this.actualHeight = actualHeight;
            this.actualPerEyeWidth = actualPerEyeWidth;
            this.downscale = downscale;
            this.contentAspect = contentAspect;
            this.clamped = clamped;
            this.widthLimited = widthLimited;
            this.heightLimited = heightLimited;
            this.pixelLimited = pixelLimited;
        }
    }

    public static final class Configuration {
        public final boolean enabled;
        public final int presentationMode;
        public final String presentationModeName;
        public final float immersiveViewScale;
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
        public final float stereoSourceWidthMultiplier;
        public final int maxStereoSourceWidth;
        public final boolean touchControllerEnabled;
        public final boolean debugLogging;
        public final boolean startupProofLayers;

        private Configuration(boolean enabled, int presentationMode, String presentationModeName,
                float immersiveViewScale, boolean stereoEnabled, boolean swapEyes, float ipdMeters,
                float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
                float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
                float cameraOffsetZMeters, float nearPlane, float farPlane,
                float screenDistanceMeters, float screenScale, String hudMode,
                float cullingExpansion,
                boolean useOpenXrFov, boolean marioKartProfileEnabled,
                float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
                float stereoSourceWidthMultiplier, int maxStereoSourceWidth,
                boolean touchControllerEnabled, boolean debugLogging,
                boolean startupProofLayers) {
            this.enabled = enabled;
            this.presentationMode = presentationMode;
            this.presentationModeName = presentationModeName;
            this.immersiveViewScale = immersiveViewScale;
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
            this.stereoSourceWidthMultiplier = stereoSourceWidthMultiplier;
            this.maxStereoSourceWidth = maxStereoSourceWidth;
            this.touchControllerEnabled = touchControllerEnabled;
            this.debugLogging = debugLogging;
            this.startupProofLayers = startupProofLayers;
        }
    }
}
