package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Bundle;
import android.os.IBinder;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import paulscode.android.mupen64plusae.dialog.ProgressDialog;
import paulscode.android.mupen64plusae.persistent.AppData;
import paulscode.android.mupen64plusae.persistent.ConfigFile;
import paulscode.android.mupen64plusae.persistent.GlobalPrefs;
import paulscode.android.mupen64plusae.questvr.QuestVrBridge;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;
import paulscode.android.mupen64plusae.task.CacheRomInfoService;
import paulscode.android.mupen64plusae.task.GalleryRefreshTask;

/**
 * Small Quest-native front door for the existing emulator library.
 *
 * The shell is intentionally a mono Android Canvas surface presented on the proven world-locked
 * OpenXR quad. It never enters GLideN64 and therefore cannot alter packed-SBS game rendering.
 */
public final class QuestVrLauncherActivity extends AppCompatActivity
        implements CacheRomInfoService.CacheRomInfoListener {
    private static final String TAG = "QuestVrLauncher";
    private static WeakReference<QuestVrLauncherActivity> sActiveInstance =
            new WeakReference<>(null);
    private static final int ROW_LIBRARY = 0;
    private static final int ROW_MODE = 1;
    private static final int ROW_ACTIONS = 2;
    private static final int ACTION_LAUNCH = 0;
    private static final int ACTION_IMPORT = 1;
    private static final int ACTION_ADVANCED = 2;
    private static final int MAX_ARTWORK_CACHE = 8;

    private final Object mMenuLock = new Object();
    private final Map<String, Bitmap> mArtworkCache =
            new LinkedHashMap<String, Bitmap>(MAX_ARTWORK_CACHE, 0.75f, true) {
                @Override
                protected boolean removeEldestEntry(Map.Entry<String, Bitmap> eldest) {
                    return size() > MAX_ARTWORK_CACHE;
                }
            };

    private QuestVrLauncherSurface mVrSurface;
    private AppData mAppData;
    private GlobalPrefs mGlobalPrefs;
    private ConfigFile mConfig;
    private List<GalleryItem> mGames = Collections.emptyList();
    private int mSelectedGame;
    private int mSelectedMode;
    private int mSelectedAction;
    private int mFocusRow = ROW_LIBRARY;
    private int mMenuRevision;
    private boolean mLibraryLoading = true;
    private boolean mScanning;
    private volatile boolean mTransitionPending;
    private String mStatus = "Loading library";
    private ProgressDialog mScanProgress;
    private CacheRomInfoService mScanService;
    private ServiceConnection mScanConnection;

    private final ActivityResultLauncher<Intent> mScanLauncher = registerForActivityResult(
            new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == Activity.RESULT_OK && result.getData() != null) {
                    beginRomScan(result.getData());
                } else {
                    setStatus("Import cancelled");
                    loadLibraryAsync();
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sActiveInstance = new WeakReference<>(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON |
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        mAppData = new AppData(this);
        mGlobalPrefs = new GlobalPrefs(this, mAppData);
        mConfig = new ConfigFile(mGlobalPrefs.romInfoCacheCfg);
        final SharedPreferences vrPreferences = getSharedPreferences(
                QuestVrSettings.PREFERENCES_NAME, Context.MODE_PRIVATE);
        mSelectedMode = QuestVrSettings.PRESENTATION_IMMERSIVE_PROJECTION.equals(
                vrPreferences.getString("presentation_mode",
                        QuestVrSettings.PRESENTATION_CINEMA_SCREEN)) ? 1 : 0;

        mVrSurface = new QuestVrLauncherSurface(this);
        setContentView(mVrSurface);
        loadLibraryAsync();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mVrSurface != null && !mTransitionPending) {
            mVrSurface.resumeVrAfterDelay();
        }
    }

    @Override
    protected void onDestroy() {
        if (mVrSurface != null) {
            mVrSurface.destroyAndWait();
        }
        dismissScanProgress();
        super.onDestroy();
        if (sActiveInstance.get() == this) {
            sActiveInstance.clear();
        }
        QuestVrGameHandoffActivity.onLauncherDestroyed();
    }

    static boolean isLifecycleOwnerAlive() {
        final QuestVrLauncherActivity activity = sActiveInstance.get();
        return activity != null && !activity.isDestroyed();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() != KeyEvent.ACTION_DOWN || event.getRepeatCount() != 0) {
            return super.dispatchKeyEvent(event);
        }
        int input = 0;
        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP:
                input = QuestVrBridge.MENU_INPUT_UP;
                break;
            case KeyEvent.KEYCODE_DPAD_DOWN:
                input = QuestVrBridge.MENU_INPUT_DOWN;
                break;
            case KeyEvent.KEYCODE_DPAD_LEFT:
                input = QuestVrBridge.MENU_INPUT_LEFT;
                break;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                input = QuestVrBridge.MENU_INPUT_RIGHT;
                break;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_DPAD_CENTER:
                input = QuestVrBridge.MENU_INPUT_SELECT;
                break;
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_BACK:
                input = QuestVrBridge.MENU_INPUT_BACK;
                break;
            default:
                break;
        }
        if (input != 0) {
            onVrMenuInput(input);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    boolean isTransitionPending() {
        return mTransitionPending;
    }

    int getMenuRevision() {
        synchronized (mMenuLock) {
            return mMenuRevision;
        }
    }

    void onVrMenuInput(int input) {
        int action = -1;
        boolean exit = false;
        boolean cancelScan = false;
        synchronized (mMenuLock) {
            if (mTransitionPending) {
                return;
            }
            if ((input & QuestVrBridge.MENU_INPUT_BACK) != 0) {
                if (mScanning) {
                    cancelScan = true;
                } else if (mFocusRow > ROW_LIBRARY) {
                    --mFocusRow;
                } else {
                    exit = true;
                }
            } else {
                if ((input & QuestVrBridge.MENU_INPUT_UP) != 0) {
                    mFocusRow = Math.max(ROW_LIBRARY, mFocusRow - 1);
                }
                if ((input & QuestVrBridge.MENU_INPUT_DOWN) != 0) {
                    mFocusRow = Math.min(ROW_ACTIONS, mFocusRow + 1);
                }
                final int direction = (input & QuestVrBridge.MENU_INPUT_LEFT) != 0 ? -1 :
                        (input & QuestVrBridge.MENU_INPUT_RIGHT) != 0 ? 1 : 0;
                if (direction != 0) {
                    if (mFocusRow == ROW_LIBRARY && !mGames.isEmpty()) {
                        mSelectedGame = wrap(mSelectedGame + direction, mGames.size());
                    } else if (mFocusRow == ROW_MODE) {
                        mSelectedMode = wrap(mSelectedMode + direction, 2);
                    } else if (mFocusRow == ROW_ACTIONS) {
                        mSelectedAction = wrap(mSelectedAction + direction, 3);
                    }
                }
                if ((input & QuestVrBridge.MENU_INPUT_SELECT) != 0) {
                    if (mFocusRow < ROW_ACTIONS) {
                        ++mFocusRow;
                    } else {
                        action = mSelectedAction;
                    }
                }
            }
            ++mMenuRevision;
        }

        if (cancelScan) {
            final CacheRomInfoService service = mScanService;
            if (service != null) {
                service.stop();
                setStatus("Cancelling scan");
            }
        } else if (exit) {
            runOnUiThread(() -> leaveVr(this::finish));
        } else if (action >= 0) {
            final int selectedAction = action;
            runOnUiThread(() -> performAction(selectedAction));
        }
    }

    void onVrInitializationFailed() {
        runOnUiThread(() -> {
            if (isFinishing() || mTransitionPending) {
                return;
            }
            QuestVrDiagnostics.warn(TAG,
                    "VR launcher OpenXR initialization failed; opening the legacy gallery");
            startActivity(new Intent(this, GalleryActivity.class));
            finish();
        });
    }

    void onVrSessionEnded() {
        runOnUiThread(() -> {
            if (!isFinishing() && !mTransitionPending) {
                finish();
            }
        });
    }

    private void performAction(int action) {
        if (mTransitionPending || mScanning) {
            return;
        }
        if (action == ACTION_LAUNCH) {
            final GalleryItem game;
            final int mode;
            synchronized (mMenuLock) {
                if (mGames.isEmpty()) {
                    action = ACTION_IMPORT;
                    game = null;
                    mode = 0;
                } else {
                    game = mGames.get(Math.min(mSelectedGame, mGames.size() - 1));
                    mode = mSelectedMode;
                }
            }
            if (action == ACTION_LAUNCH && game != null) {
                final String presentationMode = persistPresentationMode(mode);
                updateLastPlayed(game);
                final Intent handoff = QuestVrGameHandoffActivity.createGameLaunchIntent(this,
                        game.romUri, game.zipUri, game.md5, game.crc, game.headerName,
                        game.countryCode.getValue(), game.artPath, game.goodName,
                        game.displayName, presentationMode);
                leaveVr(() -> {
                    QuestVrDiagnostics.info(TAG,
                            "Starting non-XR lifecycle handoff after launcher teardown");
                    startActivity(handoff);
                    finish();
                    overridePendingTransition(0, 0);
                });
                return;
            }
        }
        if (action == ACTION_IMPORT) {
            leaveVr(() -> mScanLauncher.launch(new Intent(this, ScanRomsActivity.class)));
        } else if (action == ACTION_ADVANCED) {
            leaveVr(() -> {
                startActivity(new Intent(this, GalleryActivity.class));
                finish();
                overridePendingTransition(0, 0);
            });
        }
    }

    private void leaveVr(Runnable next) {
        if (mTransitionPending || isFinishing()) {
            return;
        }
        mTransitionPending = true;
        mVrSurface.shutdownForTransition(() -> runOnUiThread(() -> {
            try {
                next.run();
            } finally {
                mTransitionPending = false;
            }
        }));
    }

    private String persistPresentationMode(int mode) {
        final String value = mode == 0 ? QuestVrSettings.PRESENTATION_CINEMA_SCREEN :
                QuestVrSettings.PRESENTATION_IMMERSIVE_PROJECTION;
        getSharedPreferences(QuestVrSettings.PREFERENCES_NAME, Context.MODE_PRIVATE).edit()
                .putBoolean("enabled", true)
                .putBoolean("stereo_enabled", true)
                .putString("presentation_mode", value)
                .apply();
        QuestVrDiagnostics.info(TAG, "Selected launch presentation=" + value + " stereo=true");
        return value;
    }

    private void updateLastPlayed(GalleryItem game) {
        try {
            mConfig = new ConfigFile(mGlobalPrefs.romInfoCacheCfg);
            mConfig.put(game.md5, "lastPlayed",
                    Integer.toString((int) (new Date().getTime() / 1000)));
            mConfig.save();
        } catch (RuntimeException exception) {
            QuestVrDiagnostics.warn(TAG, "Unable to update last-played metadata: " + exception);
        }
    }

    private void loadLibraryAsync() {
        synchronized (mMenuLock) {
            mLibraryLoading = true;
            mStatus = mScanning ? "Scanning library" : "Loading library";
            ++mMenuRevision;
        }
        final String selectedMd5;
        synchronized (mMenuLock) {
            selectedMd5 = mGames.isEmpty() ? null :
                    mGames.get(Math.min(mSelectedGame, mGames.size() - 1)).md5;
        }
        final Thread loader = new Thread(() -> {
            final AppData appData = new AppData(this);
            final GlobalPrefs globalPrefs = new GlobalPrefs(this, appData);
            final ConfigFile config = new ConfigFile(globalPrefs.romInfoCacheCfg);
            final List<GalleryItem> items = new ArrayList<>();
            final List<GalleryItem> allItems = new ArrayList<>();
            final List<GalleryItem> recentItems = new ArrayList<>();
            new GalleryRefreshTask(null, this, globalPrefs, "", config)
                    .generateGridItemsAndSaveConfig(items, allItems, recentItems);
            items.removeIf(item -> item == null || item.isHeading || item.romUri == null);
            runOnUiThread(() -> {
                synchronized (mMenuLock) {
                    mAppData = appData;
                    mGlobalPrefs = globalPrefs;
                    mConfig = config;
                    mGames = Collections.unmodifiableList(new ArrayList<>(items));
                    mSelectedGame = 0;
                    if (selectedMd5 != null) {
                        for (int index = 0; index < mGames.size(); ++index) {
                            if (selectedMd5.equals(mGames.get(index).md5)) {
                                mSelectedGame = index;
                                break;
                            }
                        }
                    }
                    mLibraryLoading = false;
                    mStatus = mGames.isEmpty() ? "Import a game to begin" :
                            mGames.size() + (mGames.size() == 1 ? " game" : " games");
                    if (mGames.isEmpty()) {
                        mFocusRow = ROW_ACTIONS;
                        mSelectedAction = ACTION_IMPORT;
                    }
                    ++mMenuRevision;
                }
            });
        }, "VR library refresh");
        loader.setDaemon(true);
        loader.start();
    }

    private void beginRomScan(Intent data) {
        final Bundle extras = data.getExtras();
        if (extras == null) {
            setStatus("Nothing selected for import");
            return;
        }
        final String searchUri = extras.getString(ActivityHelper.Keys.SEARCH_PATH);
        if (TextUtils.isEmpty(searchUri)) {
            setStatus("Nothing selected for import");
            return;
        }
        final boolean searchZips = extras.getBoolean(ActivityHelper.Keys.SEARCH_ZIPS);
        final boolean downloadArt = extras.getBoolean(ActivityHelper.Keys.DOWNLOAD_ART);
        final boolean clearGallery = extras.getBoolean(ActivityHelper.Keys.CLEAR_GALLERY);
        final boolean searchSubdirectories = extras.getBoolean(ActivityHelper.Keys.SEARCH_SUBDIR);
        final boolean singleFile = extras.getBoolean(ActivityHelper.Keys.SEARCH_SINGLE_FILE);

        synchronized (mMenuLock) {
            mScanning = true;
            mStatus = "Scanning library";
            ++mMenuRevision;
        }
        mScanProgress = new ProgressDialog(this, getString(R.string.scanning_title),
                "VR library", getString(R.string.toast_pleaseWait), true);
        mScanConnection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder binder) {
                final CacheRomInfoService.LocalBinder localBinder =
                        (CacheRomInfoService.LocalBinder) binder;
                mScanService = localBinder.getService();
                mScanService.SetCacheRomInfoListener(QuestVrLauncherActivity.this);
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                mScanService = null;
            }
        };
        ActivityHelper.startCacheRomInfoService(getApplicationContext(), mScanConnection,
                searchUri, mAppData.mupen64plus_ini, mGlobalPrefs.romInfoCacheCfg,
                mGlobalPrefs.coverArtDir, searchZips, downloadArt, clearGallery,
                searchSubdirectories, singleFile);
    }

    @Override
    public void onCacheRomInfoFinished() {
        setStatus("Finishing library scan");
    }

    @Override
    public void onCacheRomInfoServiceDestroyed() {
        runOnUiThread(() -> {
            mScanService = null;
            synchronized (mMenuLock) {
                mScanning = false;
                ++mMenuRevision;
            }
            dismissScanProgress();
            if (mScanConnection != null) {
                try {
                    unbindService(mScanConnection);
                } catch (IllegalArgumentException ignored) {
                    // The stopped service may already have released the binding.
                }
                mScanConnection = null;
            }
            loadLibraryAsync();
        });
    }

    @Override
    public ProgressDialog GetProgressDialog() {
        if (mScanProgress == null) {
            mScanProgress = new ProgressDialog(this, getString(R.string.scanning_title),
                    "VR library", getString(R.string.toast_pleaseWait), true);
        }
        return mScanProgress;
    }

    private void dismissScanProgress() {
        if (mScanProgress != null) {
            mScanProgress.dismiss();
            mScanProgress = null;
        }
    }

    private void setStatus(String status) {
        runOnUiThread(() -> {
            synchronized (mMenuLock) {
                mStatus = status;
                ++mMenuRevision;
            }
        });
    }

    void drawVrMenu(@NonNull Canvas canvas) {
        final List<GalleryItem> games;
        final int gameIndex;
        final int mode;
        final int action;
        final int focus;
        final boolean loading;
        final boolean scanning;
        final String status;
        synchronized (mMenuLock) {
            games = mGames;
            gameIndex = mSelectedGame;
            mode = mSelectedMode;
            action = mSelectedAction;
            focus = mFocusRow;
            loading = mLibraryLoading;
            scanning = mScanning;
            status = mStatus;
        }

        final float sx = canvas.getWidth() / (float) QuestVrLauncherSurface.MENU_WIDTH;
        final float sy = canvas.getHeight() / (float) QuestVrLauncherSurface.MENU_HEIGHT;
        canvas.save();
        canvas.scale(sx, sy);
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        paint.setShader(new LinearGradient(0, 0, 1600, 900,
                Color.rgb(2, 6, 17), Color.rgb(5, 24, 48), Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, 1600, 900, paint);
        paint.setShader(new RadialGradient(800, 410, 650,
                new int[]{Color.argb(100, 0, 150, 255), Color.argb(20, 0, 90, 180),
                        Color.TRANSPARENT}, null, Shader.TileMode.CLAMP));
        canvas.drawCircle(800, 410, 650, paint);
        paint.setShader(null);

        drawText(canvas, paint, getString(R.string.app_name) + " VR", 72, 91, 50,
                Color.WHITE, Paint.Align.LEFT, true);
        drawText(canvas, paint, "QUEST LIBRARY", 75, 125, 17,
                Color.rgb(54, 202, 255), Paint.Align.LEFT, true);
        drawText(canvas, paint, status, 1525, 100, 23,
                Color.rgb(167, 207, 229), Paint.Align.RIGHT, false);

        final GalleryItem selected = games.isEmpty() ? null :
                games.get(Math.min(gameIndex, games.size() - 1));
        drawLibraryRow(canvas, paint, games, gameIndex, selected, focus == ROW_LIBRARY,
                loading || scanning);
        drawModeRow(canvas, paint, mode, focus == ROW_MODE);
        drawActionRow(canvas, paint, action, focus == ROW_ACTIONS, games.isEmpty(), scanning);

        final String hint = scanning ? "Scanning in the background • B cancels" :
                "LEFT STICK  navigate     A / TRIGGER  select     B  back     BOTH STICKS  recenter";
        drawText(canvas, paint, hint, 800, 858, 18,
                Color.rgb(125, 166, 190), Paint.Align.CENTER, false);
        canvas.restore();
    }

    private void drawLibraryRow(Canvas canvas, Paint paint, List<GalleryItem> games, int index,
            GalleryItem selected, boolean focused, boolean busy) {
        final RectF center = new RectF(420, 155, 1180, 555);
        drawPanel(canvas, paint, center, focused, true);
        if (!games.isEmpty()) {
            final GalleryItem previous = games.get(wrap(index - 1, games.size()));
            final GalleryItem next = games.get(wrap(index + 1, games.size()));
            drawSideCard(canvas, paint, new RectF(80, 210, 350, 510), previous.toString(),
                    games.size() > 1);
            drawSideCard(canvas, paint, new RectF(1250, 210, 1520, 510), next.toString(),
                    games.size() > 1);
        }

        if (selected == null) {
            drawText(canvas, paint, busy ? "Preparing your library…" : "No games imported yet",
                    800, 330, 34, Color.WHITE, Paint.Align.CENTER, true);
            drawText(canvas, paint, busy ? "" : "Choose Import Games below",
                    800, 382, 22, Color.rgb(145, 190, 216), Paint.Align.CENTER, false);
            return;
        }

        final RectF artRect = new RectF(455, 185, 730, 525);
        final Bitmap artwork = getArtwork(selected);
        if (artwork != null) {
            canvas.save();
            final Path artworkClip = new Path();
            artworkClip.addRoundRect(artRect, 22, 22, Path.Direction.CW);
            canvas.clipPath(artworkClip);
            final float scale = Math.max(artRect.width() / artwork.getWidth(),
                    artRect.height() / artwork.getHeight());
            final float width = artwork.getWidth() * scale;
            final float height = artwork.getHeight() * scale;
            final RectF destination = new RectF(
                    artRect.centerX() - width * 0.5f, artRect.centerY() - height * 0.5f,
                    artRect.centerX() + width * 0.5f, artRect.centerY() + height * 0.5f);
            canvas.drawBitmap(artwork, null, destination, paint);
            canvas.restore();
        } else {
            paint.setShader(new LinearGradient(455, 185, 730, 525,
                    Color.rgb(7, 60, 100), Color.rgb(7, 16, 38), Shader.TileMode.CLAMP));
            canvas.drawRoundRect(artRect, 22, 22, paint);
            paint.setShader(null);
            canvas.drawCircle(artRect.centerX(), artRect.centerY(), 82,
                    colorPaint(paint, Color.argb(130, 0, 180, 255), Paint.Style.STROKE, 5));
            final String initial = selected.toString().isEmpty() ? "64" :
                    selected.toString().substring(0, 1).toUpperCase();
            drawText(canvas, paint, initial, artRect.centerX(), artRect.centerY() + 31,
                    82, Color.WHITE, Paint.Align.CENTER, true);
        }

        drawText(canvas, paint, ellipsize(paint, selected.toString(), 370, 37),
                770, 270, 37, Color.WHITE, Paint.Align.LEFT, true);
        drawText(canvas, paint, "NINTENDO 64", 772, 314, 18,
                Color.rgb(65, 203, 255), Paint.Align.LEFT, true);
        drawText(canvas, paint, (index + 1) + "  /  " + games.size(), 772, 382, 22,
                Color.rgb(155, 194, 216), Paint.Align.LEFT, false);
        drawText(canvas, paint, "Move left or right to browse", 772, 466, 19,
                Color.rgb(116, 158, 184), Paint.Align.LEFT, false);
    }

    private void drawModeRow(Canvas canvas, Paint paint, int mode, boolean focused) {
        drawText(canvas, paint, "PRESENTATION", 110, 625, 18,
                Color.rgb(115, 169, 199), Paint.Align.LEFT, true);
        drawChoice(canvas, paint, new RectF(390, 583, 790, 665), "STEREO CINEMA",
                "Fixed screen • recommended", mode == 0, focused);
        drawChoice(canvas, paint, new RectF(810, 583, 1210, 665), "EXPERIMENTAL IMMERSIVE",
                "Full head camera", mode == 1, focused);
    }

    private void drawActionRow(Canvas canvas, Paint paint, int action, boolean focused,
            boolean empty, boolean scanning) {
        final String launch = empty ? "IMPORT TO BEGIN" : "LAUNCH";
        drawButton(canvas, paint, new RectF(305, 710, 615, 790), launch,
                action == ACTION_LAUNCH, focused, !scanning);
        drawButton(canvas, paint, new RectF(645, 710, 955, 790), "IMPORT GAMES",
                action == ACTION_IMPORT, focused, !scanning);
        drawButton(canvas, paint, new RectF(985, 710, 1295, 790), "ADVANCED / LEGACY",
                action == ACTION_ADVANCED, focused, !scanning);
    }

    private void drawChoice(Canvas canvas, Paint paint, RectF rect, String title,
            String subtitle, boolean selected, boolean focused) {
        drawPanel(canvas, paint, rect, selected && focused, selected);
        drawText(canvas, paint, title, rect.centerX(), rect.top + 34, 19,
                selected ? Color.WHITE : Color.rgb(174, 199, 214), Paint.Align.CENTER, true);
        drawText(canvas, paint, subtitle, rect.centerX(), rect.top + 61, 15,
                Color.rgb(104, 155, 184), Paint.Align.CENTER, false);
    }

    private void drawButton(Canvas canvas, Paint paint, RectF rect, String title,
            boolean selected, boolean focused, boolean enabled) {
        drawPanel(canvas, paint, rect, selected && focused, selected);
        drawText(canvas, paint, title, rect.centerX(), rect.centerY() + 7, 19,
                enabled ? (selected ? Color.WHITE : Color.rgb(174, 199, 214)) :
                        Color.rgb(75, 104, 122), Paint.Align.CENTER, true);
    }

    private void drawSideCard(Canvas canvas, Paint paint, RectF rect, String name,
            boolean visible) {
        if (!visible) {
            return;
        }
        paint.setColor(Color.argb(95, 8, 30, 54));
        paint.setStyle(Paint.Style.FILL);
        canvas.drawRoundRect(rect, 24, 24, paint);
        drawText(canvas, paint, ellipsize(paint, name, rect.width() - 32, 20),
                rect.centerX(), rect.bottom - 28, 20, Color.rgb(116, 158, 184),
                Paint.Align.CENTER, false);
    }

    private void drawPanel(Canvas canvas, Paint paint, RectF rect, boolean focused,
            boolean selected) {
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(rect.left, rect.top, rect.right, rect.bottom,
                selected ? Color.argb(235, 8, 48, 83) : Color.argb(220, 5, 25, 47),
                Color.argb(235, 3, 13, 31), Shader.TileMode.CLAMP));
        canvas.drawRoundRect(rect, 25, 25, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(focused ? 5 : selected ? 3 : 2);
        paint.setColor(focused ? Color.rgb(53, 211, 255) : selected ?
                Color.argb(190, 19, 158, 222) : Color.argb(85, 52, 126, 168));
        canvas.drawRoundRect(rect, 25, 25, paint);
        if (focused) {
            paint.setStrokeWidth(14);
            paint.setColor(Color.argb(35, 35, 190, 255));
            canvas.drawRoundRect(rect, 29, 29, paint);
        }
        paint.setStyle(Paint.Style.FILL);
    }

    private Bitmap getArtwork(GalleryItem item) {
        if (TextUtils.isEmpty(item.artPath) || !new File(item.artPath).exists()) {
            return null;
        }
        synchronized (mArtworkCache) {
            Bitmap bitmap = mArtworkCache.get(item.artPath);
            if (bitmap == null) {
                bitmap = BitmapFactory.decodeFile(item.artPath);
                if (bitmap != null) {
                    mArtworkCache.put(item.artPath, bitmap);
                }
            }
            return bitmap;
        }
    }

    private static Paint colorPaint(Paint paint, int color, Paint.Style style, float width) {
        paint.setShader(null);
        paint.setColor(color);
        paint.setStyle(style);
        paint.setStrokeWidth(width);
        return paint;
    }

    private static void drawText(Canvas canvas, Paint paint, String text, float x, float y,
            float size, int color, Paint.Align align, boolean bold) {
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        paint.setTextSize(size);
        paint.setTextAlign(align);
        paint.setTypeface(bold ? android.graphics.Typeface.DEFAULT_BOLD :
                android.graphics.Typeface.DEFAULT);
        canvas.drawText(text == null ? "" : text, x, y, paint);
    }

    private static String ellipsize(Paint paint, String value, float maxWidth, float textSize) {
        if (value == null) {
            return "Unknown game";
        }
        paint.setTextSize(textSize);
        if (paint.measureText(value) <= maxWidth) {
            return value;
        }
        String shortened = value;
        while (shortened.length() > 1 && paint.measureText(shortened + "…") > maxWidth) {
            shortened = shortened.substring(0, shortened.length() - 1);
        }
        return shortened + "…";
    }

    private static int wrap(int value, int count) {
        if (count <= 0) {
            return 0;
        }
        final int result = value % count;
        return result < 0 ? result + count : result;
    }
}
