/*
 mal
 https://github.com/brackeen/mal
 Copyright (c) 2014-2016 David Brackeen
 
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef _MAL_AUDIO_ABSTRACT_H_
#define _MAL_AUDIO_ABSTRACT_H_

#include "mal.h"
#include "ok_lib.h"

// If MAL_USE_MUTEX is defined, modifications to mal_player objects are locked.
// Define MAL_USE_MUTEX if a player's buffer data is read on a different thread than the main
// thread.
#ifdef MAL_USE_MUTEX
#  include <pthread.h>
#    define MAL_LOCK(player) pthread_mutex_lock(&player->mutex)
#    define MAL_UNLOCK(player) pthread_mutex_unlock(&player->mutex)
#  else
#    define MAL_LOCK(player) do { } while(0)
#    define MAL_UNLOCK(player) do { } while(0)
#endif

//#define MAL_DEBUG_LOG
#ifdef MAL_DEBUG_LOG
#  ifdef ANDROID
#    include <android/log.h>
#    define MAL_LOG(...) __android_log_print(ANDROID_LOG_INFO, "mal", __VA_ARGS__)
#  else
#    define MAL_LOG(...) do { printf("mal: " __VA_ARGS__); printf("\n"); } while(0)
#  endif
#else
#  define MAL_LOG(...) do { } while(0)
#endif

// Audio subsystems need to implement these structs and functions.
// All mal_*init() functions should return `true` on success, `false` otherwise.

struct _mal_context;
struct _mal_buffer;
struct _mal_player;

static bool _mal_context_init(mal_context *context);
static void _mal_context_did_create(mal_context *context);
static void _mal_context_will_dispose(mal_context *context);
static void _mal_context_dispose(mal_context *context);
static void _mal_context_did_set_active(mal_context *context, const bool active);
static void _mal_context_dispose(mal_context *context);
static void _mal_context_set_active(mal_context *context, const bool active);
static void _mal_context_set_mute(mal_context *context, const bool mute);
static void _mal_context_set_gain(mal_context *context, const float gain);

/**
 Either `copied_data` or `managed_data` will be non-null, but not both. If `copied_data` is set,
 the data must be copied (don't keep a reference to `copied_data`).
 If successful, the buffer's #managed_data and #managed_data_deallocator fields must be set.
 */
static bool _mal_buffer_init(mal_context *context, mal_buffer *buffer,
                             const void *copied_data, void *managed_data,
                             mal_deallocator_func data_deallocator);
static void _mal_buffer_dispose(mal_buffer *buffer);

static bool _mal_player_init(mal_player *player);
static void _mal_player_dispose(mal_player *player);
static bool _mal_player_set_format(mal_player *player, mal_format format);
static bool _mal_player_set_buffer(mal_player *player, const mal_buffer *buffer);
static void _mal_player_set_mute(mal_player *player, bool mute);
static void _mal_player_set_gain(mal_player *player, float gain);
static void _mal_player_set_looping(mal_player *player, bool looping);
static void _mal_player_did_set_finished_callback(mal_player *player);
static mal_player_state _mal_player_get_state(const mal_player *player);
static bool _mal_player_set_state(mal_player *player, mal_player_state old_state,
                                  mal_player_state state);

// MARK: Globals

typedef struct ok_vec_of(mal_player *) mal_player_vec_t;
typedef struct ok_vec_of(mal_buffer *) mal_buffer_vec_t;
typedef struct ok_map_of(uint64_t, mal_player *) mal_callback_map_t;

static mal_callback_map_t *global_active_callbacks = NULL;
static uint64_t next_finished_callback_id = 1;
#ifdef MAL_USE_MUTEX
static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// MARK: Structs

struct mal_context {
    mal_player_vec_t players;
    mal_buffer_vec_t buffers;
    bool routes[NUM_MAL_ROUTES];
    float gain;
    bool mute;
    bool active;
    double sample_rate;

#ifdef MAL_USE_MUTEX
    pthread_mutex_t mutex;
#endif

    struct _mal_context data;
};

struct mal_buffer {
    mal_context *context;
    mal_format format;
    uint32_t num_frames;
    void *managed_data;
    mal_deallocator_func managed_data_deallocator;

    struct _mal_buffer data;
};

struct mal_player {
    mal_context *context;
    mal_format format;
    const mal_buffer *buffer;
    float gain;
    bool mute;
    bool looping;

    mal_playback_finished_func on_finished;
    void *on_finished_user_data;
    uint64_t on_finished_id;

#ifdef MAL_USE_MUTEX
    pthread_mutex_t mutex;
#endif

    struct _mal_player data;
};

// MARK: Context

mal_context *mal_context_create(double output_sample_rate) {
    mal_context *context = calloc(1, sizeof(mal_context));
    if (context) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&context->mutex, NULL);
#endif
        context->mute = false;
        context->gain = 1.0f;
        context->sample_rate = output_sample_rate;
        ok_vec_init(&context->players);
        ok_vec_init(&context->buffers);
        bool success = _mal_context_init(context);
        if (success) {
            _mal_context_did_create(context);
            mal_context_set_active(context, true);
        } else {
            mal_context_free(context);
            context = NULL;
        }
    }
    return context;
}

void mal_context_set_active(mal_context *context, const bool active) {
    if (context) {
        MAL_LOCK(context);
        _mal_context_set_active(context, active);
        context->active = active;
        MAL_UNLOCK(context);
        _mal_context_did_set_active(context, active);
    }
}

bool mal_context_get_mute(const mal_context *context) {
    return context ? context->mute : false;
}

void mal_context_set_mute(mal_context *context, const bool mute) {
    if (context) {
        context->mute = mute;
        _mal_context_set_mute(context, mute);
    }
}

float mal_context_get_gain(const mal_context *context) {
    return context ? context->gain : 1.0f;
}

void mal_context_set_gain(mal_context *context, const float gain) {
    if (context) {
        context->gain = gain;
        _mal_context_set_gain(context, gain);
    }
}

bool mal_context_format_is_valid(const mal_context *context, const mal_format format) {
    // TODO: Move to subsystem
    return ((format.bit_depth == 8 || format.bit_depth == 16) &&
            (format.num_channels == 1 || format.num_channels == 2) &&
            format.sample_rate > 0);
}

bool mal_context_is_route_enabled(const mal_context *context, const mal_route route) {
    if (context && route < NUM_MAL_ROUTES) {
        return context->routes[route];
    } else {
        return false;
    }
}

void mal_context_free(mal_context *context) {
    if (context) {
        // Delete players
        ok_vec_foreach(&context->players, mal_player *player) {
            mal_player_set_buffer(player, NULL);
            MAL_LOCK(player);
            _mal_player_dispose(player);
            player->context = NULL;
            MAL_UNLOCK(player);
        }
        ok_vec_deinit(&context->players);

        // Delete buffers
        ok_vec_foreach(&context->buffers, mal_buffer *buffer) {
            _mal_buffer_dispose(buffer);
            buffer->context = NULL;
        }
        ok_vec_deinit(&context->buffers);

        // Dispose and free
        _mal_context_will_dispose(context);
        mal_context_set_active(context, false);
        MAL_LOCK(context);
        _mal_context_dispose(context);
        MAL_UNLOCK(context);

#ifdef MAL_USE_MUTEX
        pthread_mutex_destroy(&context->mutex);
#endif
        free(context);
    }
}

bool mal_formats_equal(const mal_format format1, const mal_format format2) {
    return (format1.bit_depth == format2.bit_depth &&
            format1.num_channels == format2.num_channels &&
            format1.sample_rate == format2.sample_rate);
}

// MARK: Buffer

static mal_buffer *_mal_buffer_create_internal(mal_context *context, const mal_format format,
                                               const uint32_t num_frames, const void *copied_data,
                                               void *managed_data,
                                               const mal_deallocator_func data_deallocator) {
    // Check params
    const bool oneNonNullData = (copied_data == NULL) != (managed_data == NULL);
    if (!context || !mal_context_format_is_valid(context, format) || num_frames == 0 ||
        !oneNonNullData) {
        return NULL;
    }
    mal_buffer *buffer = calloc(1, sizeof(mal_buffer));
    if (buffer) {
        ok_vec_push(&context->buffers, buffer);
        buffer->context = context;
        buffer->format = format;
        buffer->num_frames = num_frames;

        bool success = _mal_buffer_init(context, buffer, copied_data, managed_data,
                                        data_deallocator);
        if (!success) {
            mal_buffer_free(buffer);
            buffer = NULL;
        }
    }
    return buffer;
}

mal_buffer *mal_buffer_create(mal_context *context, const mal_format format,
                              const uint32_t num_frames, const void *data) {
    return _mal_buffer_create_internal(context, format, num_frames, data, NULL, NULL);
}

mal_buffer *mal_buffer_create_no_copy(mal_context *context, const mal_format format,
                                      const uint32_t num_frames, void *data,
                                      const mal_deallocator_func data_deallocator) {
    return _mal_buffer_create_internal(context, format, num_frames, NULL, data, data_deallocator);
}

mal_format mal_buffer_get_format(const mal_buffer *buffer) {
    if (buffer) {
        return buffer->format;
    } else {
        static const mal_format null_format = {0, 0, 0};
        return null_format;
    }
}

uint32_t mal_buffer_get_num_frames(const mal_buffer *buffer) {
    return buffer ? buffer->num_frames : 0;
}

void *mal_buffer_get_data(const mal_buffer *buffer) {
    return buffer ? buffer->managed_data : NULL;
}

void mal_buffer_free(mal_buffer *buffer) {
    if (buffer) {
        if (buffer->context) {
            // First, stop all players that are using this buffer.
            ok_vec_foreach(&buffer->context->players, mal_player *player) {
                if (player->buffer == buffer) {
                    mal_player_set_buffer(player, NULL);
                }
            }
            ok_vec_remove(&buffer->context->buffers, buffer);
        }
        _mal_buffer_dispose(buffer);
        if (buffer->managed_data) {
            if (buffer->managed_data_deallocator) {
                buffer->managed_data_deallocator(buffer->managed_data);
            }
            buffer->managed_data = NULL;
        }
        free(buffer);
    }
}

// MARK: Player

mal_player *mal_player_create(mal_context *context, const mal_format format) {
    // Check params
    if (!context || !mal_context_format_is_valid(context, format)) {
        return NULL;
    }
    mal_player *player = calloc(1, sizeof(mal_player));
    if (player) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_init(&player->mutex, NULL);
#endif
        ok_vec_push(&context->players, player);
        player->context = context;
        player->format = format;
        player->gain = 1.0f;

        bool success = _mal_player_init(player);
        if (success) {
            success = _mal_player_set_format(player, format);
        }
        if (!success) {
            mal_player_free(player);
            player = NULL;
        }
    }
    return player;
}

mal_format mal_player_get_format(const mal_player *player) {
    if (player) {
        return player->format;
    } else {
        static const mal_format null_format = {0, 0, 0};
        return null_format;
    }
}

bool mal_player_set_format(mal_player *player, mal_format format) {
    if (player && mal_context_format_is_valid(player->context, format)) {
        mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
        MAL_LOCK(player);
        bool success = _mal_player_set_format(player, format);
        if (success) {
            player->format = format;
        }
        MAL_UNLOCK(player);
        return success;
    }
    return false;
}

bool mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    if (!player) {
        return false;
    } else {
        mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
        MAL_LOCK(player);
        bool success = _mal_player_set_buffer(player, buffer);
        if (success) {
            player->buffer = buffer;
        } else {
            player->buffer = NULL;
        }
        MAL_UNLOCK(player);
        return success;
    }
}

const mal_buffer *mal_player_get_buffer(const mal_player *player) {
    return player ? player->buffer : NULL;
}

void mal_player_set_finished_func(mal_player *player, mal_playback_finished_func on_finished,
                                  void *user_data) {
    if (player) {
#ifdef MAL_USE_MUTEX
        pthread_mutex_lock(&global_mutex);
#endif
        if (!global_active_callbacks) {
            global_active_callbacks = malloc(sizeof(*global_active_callbacks));
            ok_map_init_custom(global_active_callbacks, ok_uint64_hash, ok_64bit_equals);
        }
        if (player->on_finished_id) {
            ok_map_remove(global_active_callbacks, player->on_finished_id);
        }
        player->on_finished = on_finished;
        player->on_finished_user_data = user_data;
        if (on_finished != NULL) {
            player->on_finished_id = next_finished_callback_id;
            next_finished_callback_id++;
            ok_map_put(global_active_callbacks, player->on_finished_id, player);
        } else {
            player->on_finished_id = 0;
        }
#ifdef MAL_USE_MUTEX
        pthread_mutex_unlock(&global_mutex);
#endif
        _mal_player_did_set_finished_callback(player);
    }
}

mal_playback_finished_func mal_player_get_finished_func(mal_player *player) {
    return player ? player->on_finished : NULL;
}

static void _mal_handle_on_finished_callback(uint64_t on_finished_id) {
    // Find the player
    mal_player *player = NULL;
#ifdef MAL_USE_MUTEX
    pthread_mutex_lock(&global_mutex);
#endif
    if (global_active_callbacks) {
        player = ok_map_get(global_active_callbacks, on_finished_id);
    }
#ifdef MAL_USE_MUTEX
    pthread_mutex_unlock(&global_mutex);
#endif
    // Send callback
    if (player && player->on_finished) {
        player->on_finished(player->on_finished_user_data, player);
    }
}

bool mal_player_get_mute(const mal_player *player) {
    return player && player->mute;
}

void mal_player_set_mute(mal_player *player, bool mute) {
    if (player) {
        MAL_LOCK(player);
        player->mute = mute;
        _mal_player_set_mute(player, mute);
        MAL_UNLOCK(player);
    }
}

float mal_player_get_gain(const mal_player *player) {
    return player ? player->gain : 1.0f;
}

void mal_player_set_gain(mal_player *player, float gain) {
    if (player) {
        MAL_LOCK(player);
        player->gain = gain;
        _mal_player_set_gain(player, gain);
        MAL_UNLOCK(player);
    }
}

bool mal_player_is_looping(const mal_player *player) {
    return player ? player->looping : false;
}

void mal_player_set_looping(mal_player *player, bool looping) {
    if (player) {
        MAL_LOCK(player);
        player->looping = looping;
        _mal_player_set_looping(player, looping);
        MAL_UNLOCK(player);
    }
}

bool mal_player_set_state(mal_player *player, mal_player_state state) {
    if (!player || !player->buffer) {
        return false;
    } else {
        MAL_LOCK(player);
        mal_player_state old_state = _mal_player_get_state(player);
        bool success = true;
        if (state != old_state) {
            success = _mal_player_set_state(player, old_state, state);
        }
        MAL_UNLOCK(player);
        return success;
    }
}

mal_player_state mal_player_get_state(mal_player *player) {
    if (!player || !player->buffer) {
        return MAL_PLAYER_STATE_STOPPED;
    } else {
        MAL_LOCK(player);
        mal_player_state state = _mal_player_get_state(player);
        MAL_UNLOCK(player);
        return state;
    }
}

void mal_player_free(mal_player *player) {
    if (player) {
        mal_player_set_buffer(player, NULL);
        MAL_LOCK(player);
        if (player->context) {
            ok_vec_remove(&player->context->players, player);
            player->context = NULL;
        }
        mal_player_set_finished_func(player, NULL, NULL);
        _mal_player_dispose(player);
        MAL_UNLOCK(player);
#ifdef MAL_USE_MUTEX
        pthread_mutex_destroy(&player->mutex);
#endif
        free(player);
    }
}

#endif
