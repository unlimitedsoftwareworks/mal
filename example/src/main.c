
#include "glfm.h"
#include "mal.h"
#include "ok_wav.h"
#include <stdio.h> // For SEEK_CUR
#include <stdlib.h>

#define kMaxPlayers 16
#define kTestFreeBufferDuringPlayback 0
#define kTestAudioPause 0

typedef struct {
    mal_context *context;
    mal_buffer *buffer;
    mal_player *players[kMaxPlayers];
} mal_app;

static void on_finished(void *user_data, mal_player *player) {
    mal_app *app = user_data;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (player == app->players[i]) {
            glfmLog("FINISHED %i", i);
        }
    }
}

static void play_sound(mal_app *app, mal_buffer *buffer, float gain) {
#if kTestFreeBufferDuringPlayback
    // This is useful to test buffer freeing during playback
    if (app->buffer) {
        mal_buffer_free(app->buffer);
        app->buffer = NULL;
    }
#elif kTestAudioPause
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i]) {
            mal_player_state state = mal_player_get_state(app->players[i]);
            if (state == MAL_PLAYER_STATE_PLAYING) {
                mal_player_set_state(app->players[i], MAL_PLAYER_STATE_PAUSED);
            } else if (state == MAL_PLAYER_STATE_PAUSED) {
                mal_player_set_state(app->players[i], MAL_PLAYER_STATE_PLAYING);
            }
        }
    }
#else
    // Stop any looping players
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] && mal_player_get_state(app->players[i]) == MAL_PLAYER_STATE_PLAYING &&
            mal_player_is_looping(app->players[i])) {
            mal_player_set_looping(app->players[i], false);
        }
    }
    // Play new sound
    for (int i = 0; i < kMaxPlayers; i++) {
        if (app->players[i] && mal_player_get_state(app->players[i]) == MAL_PLAYER_STATE_STOPPED) {
            mal_player_set_buffer(app->players[i], buffer);
            mal_player_set_gain(app->players[i], gain);
            mal_player_set_finished_func(app->players[i], on_finished, app);
            mal_player_set_state(app->players[i], MAL_PLAYER_STATE_PLAYING);
            glfmLog("PLAY %i gain=%.2f", i, gain);
            break;
        }
    }
#endif
}

static void mal_init(mal_app *app, ok_wav *wav) {
    app->context = mal_context_create(44100);
    if (!app->context) {
        glfmLog("Error: Couldn't create audio context");
    }
    mal_format format = {
        .sample_rate = wav->sample_rate,
        .num_channels = wav->num_channels,
        .bit_depth = wav->bit_depth};
    if (!mal_context_format_is_valid(app->context, format)) {
        glfmLog("Error: Audio format is invalid");
    }
    app->buffer = mal_buffer_create_no_copy(app->context, format, (uint32_t)wav->num_frames,
                                            wav->data, free);
    if (!app->buffer) {
        glfmLog("Error: Couldn't create audio buffer");
    }
    wav->data = NULL; // Audio buffer is now managed by mal, don't free it
    ok_wav_free(wav);

    for (int i = 0; i < kMaxPlayers; i++) {
        app->players[i] = mal_player_create(app->context, format);
    }
    bool success = mal_player_set_buffer(app->players[0], app->buffer);
    if (!success) {
        glfmLog("Error: Couldn't attach buffer to audio player");
    }
    mal_player_set_gain(app->players[0], 0.25f);
    mal_player_set_looping(app->players[0], true);
    success = mal_player_set_state(app->players[0], MAL_PLAYER_STATE_PLAYING);
    if (!success) {
        glfmLog("Error: Couldn't play audio");
    }
}

// Be a good app citizen - set mal to inactive when pausing.
static void on_app_pause(GLFMDisplay *display) {
    mal_app *app = glfmGetUserData(display);
    mal_context_set_active(app->context, false);
}

static void on_app_resume(GLFMDisplay *display) {
    mal_app *app = glfmGetUserData(display);
    mal_context_set_active(app->context, true);
}

// GLFM functions

static int glfm_asset_input_func(void *user_data, unsigned char *buffer, const int count) {
    GLFMAsset *asset = user_data;
    if (buffer && count > 0) {
        return (int)glfmAssetRead(asset, buffer, count);
    } else if (glfmAssetSeek(asset, count, SEEK_CUR) == 0) {
        return count;
    } else {
        return 0;
    }
}

static GLboolean on_touch(GLFMDisplay *display, const int touch, const GLFMTouchPhase phase,
                          const int x, const int y) {
    if (phase == GLFMTouchPhaseBegan) {
        int height = glfmGetDisplayHeight(display);
        mal_app *app = glfmGetUserData(display);
        play_sound(app, app->buffer, 0.05f + 0.60f * (height - y) / height);
    }
    return GL_TRUE;
}

static void on_surface_created(GLFMDisplay *display, const int width, const int height) {
    glViewport(0, 0, width, height);
}

static void on_frame(GLFMDisplay *display, const double frameTime) {
    glClearColor(0.6f, 0.0f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glfmMain(GLFMDisplay *display) {
    mal_app *app = calloc(1, sizeof(mal_app));

    glfmSetDisplayConfig(display,
                         GLFMColorFormatRGBA8888,
                         GLFMDepthFormatNone,
                         GLFMStencilFormatNone,
                         GLFMMultisampleNone,
                         GLFMUserInterfaceChromeNavigation);
    glfmSetUserData(display, app);
    glfmSetSurfaceCreatedFunc(display, on_surface_created);
    glfmSetSurfaceResizedFunc(display, on_surface_created);
    glfmSetMainLoopFunc(display, on_frame);
    glfmSetTouchFunc(display, on_touch);
    glfmSetAppPausingFunc(display, on_app_pause);
    glfmSetAppResumingFunc(display, on_app_resume);

    GLFMAsset *asset = glfmAssetOpen("sound.wav");
    ok_wav *wav = ok_wav_read(asset, glfm_asset_input_func, true);
    glfmAssetClose(asset);

    if (!wav->data) {
        glfmLog("Error: %s", wav->error_message);
    } else {
        mal_init(app, wav);
    }
}
