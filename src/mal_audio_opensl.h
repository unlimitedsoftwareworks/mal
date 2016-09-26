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

#ifndef _MAL_AUDIO_OPENSL_H_
#define _MAL_AUDIO_OPENSL_H_

#include <SLES/OpenSLES.h>
#ifdef ANDROID
// From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
// "The buffer queue interface is expected to have significant changes... We recommend that your
// application code use Android simple buffer queues instead, because we do not plan to change
// that API"
#include <SLES/OpenSLES_Android.h>
#undef SL_DATALOCATOR_BUFFERQUEUE
#define SL_DATALOCATOR_BUFFERQUEUE SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE
#define SLDataLocator_BufferQueue SLDataLocator_AndroidSimpleBufferQueue
#define SLBufferQueueItf SLAndroidSimpleBufferQueueItf
#endif

struct _mal_context {
    SLObjectItf sl_object;
    SLEngineItf sl_engine;
    SLObjectItf sl_output_mix_object;
};

struct _mal_buffer {

};

struct _mal_player {
    SLObjectItf sl_object;
    SLPlayItf sl_play;
    SLVolumeItf sl_volume;
    SLBufferQueueItf sl_buffer_queue;
};

#define MAL_USE_MUTEX
#include "mal_audio_abstract.h"
#include <math.h>

static void _mal_player_update_gain(mal_player *player);

// MARK: Context

static bool _mal_context_init(mal_context *context, double output_sample_rate) {
    // Create engine
    SLresult result = slCreateEngine(&context->data.sl_object, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Realize engine
    result = (*context->data.sl_object)->Realize(context->data.sl_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Get engine interface
    result = (*context->data.sl_object)->GetInterface(context->data.sl_object, SL_IID_ENGINE,
                                                      &context->data.sl_engine);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Get output mix
    result = (*context->data.sl_engine)->CreateOutputMix(context->data.sl_engine,
                                                         &context->data.sl_output_mix_object, 0,
                                                         NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // Realize the output mix
    result = (*context->data.sl_output_mix_object)->Realize(context->data.sl_output_mix_object,
                                                            SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        return false;
    }

    // NOTE: SLAudioIODeviceCapabilitiesItf isn't supported, so there's no way to get routing
    // information.
    // Also, GetDestinationOutputDeviceIDs only returns SL_DEFAULTDEVICEID_AUDIOOUTPUT.
    //
    // Potentially, if we have access to the ANativeActivity, we could get an instance of
    // AudioManager and call the Java functions:
    // if (isBluetoothA2dpOn() or isBluetoothScoOn()) then wireless
    // else if (isSpeakerphoneOn()) then speaker
    // else headset
    //
    return true;
}

static void _mal_context_dispose(mal_context *context) {
    if (context->data.sl_output_mix_object) {
        (*context->data.sl_output_mix_object)->Destroy(context->data.sl_output_mix_object);
        context->data.sl_output_mix_object = NULL;
    }
    if (context->data.sl_object) {
        (*context->data.sl_object)->Destroy(context->data.sl_object);
        context->data.sl_object = NULL;
        context->data.sl_engine = NULL;
    }
}

static void _mal_context_set_active(mal_context *context, bool active) {
    if (context->active != active) {
        context->active = active;

        // From http://mobilepearls.com/labs/native-android-api/ndk/docs/opensles/
        // "Be sure to destroy your audio players when your activity is
        // paused, as they are a global resource shared with other apps."
        for (unsigned int i = 0; i < context->players.length; i++) {
            mal_player *player = (mal_player *)context->players.values[i];

            if (active) {
                if (!player->data.sl_object) {
                    mal_player_set_format(player, player->format);
                }
            } else {
                MAL_LOCK(player);
                if (player->buffer) {
                    _mal_player_set_state(player, MAL_PLAYER_STATE_STOPPED);
                }
                _mal_player_dispose(player);
                MAL_UNLOCK(player);
            }
        }
    }
}

static void _mal_context_set_mute(mal_context *context, bool mute) {
    for (unsigned int i = 0; i < context->players.length; i++) {
        _mal_player_update_gain((mal_player *)context->players.values[i]);
    }
}

static void _mal_context_set_gain(mal_context *context, float gain) {
    for (unsigned int i = 0; i < context->players.length; i++) {
        _mal_player_update_gain((mal_player *)context->players.values[i]);
    }
}

// MARK: Buffer

static bool _mal_buffer_init(mal_context *context, mal_buffer *buffer,
                             const void *copied_data, void *managed_data,
                             const mal_deallocator data_deallocator) {
    const size_t data_length = ((buffer->format.bit_depth / 8) *
                                buffer->format.num_channels * buffer->num_frames);
    if (managed_data) {
        buffer->managed_data = managed_data;
        buffer->managed_data_deallocator = data_deallocator;
    } else {
        void *new_buffer = malloc(data_length);
        if (!new_buffer) {
            return false;
        }
        memcpy(new_buffer, copied_data, data_length);
        buffer->managed_data = new_buffer;
        buffer->managed_data_deallocator = free;
    }
    return true;
}

static void _mal_buffer_dispose(mal_buffer *buffer) {
    // Do nothing
}

// MARK: Player

// Buffer queue callback, which is called on a different thread.
//
// According to the Android team, "it is unspecified whether buffer queue callbacks are called upon
// transition to SL_PLAYSTATE_STOPPED or by BufferQueue::Clear."
//
// The Android team recommends "non-blocking synchronization", but the lock will be uncontended in
// most cases.
// Also, Chromium has locks in their OpenSLES-based audio implementation.
static void _mal_buffer_queue_callback(SLBufferQueueItf queue, void *void_player) {
    mal_player *player = (mal_player *)void_player;
    if (player && queue) {
        MAL_LOCK(player);
        if (player->looping && player->buffer &&
            player->buffer->managed_data &&
            mal_player_get_state(player) == MAL_PLAYER_STATE_PLAYING) {
            const mal_buffer *buffer = player->buffer;
            const size_t len = (buffer->num_frames * (buffer->format.bit_depth / 8) *
                                buffer->format.num_channels);
            (*queue)->Enqueue(queue, buffer->managed_data, len);
        } else if (player->data.sl_play) {
            (*player->data.sl_play)->SetPlayState(player->data.sl_play, SL_PLAYSTATE_STOPPED);
        }
        MAL_UNLOCK(player);
    }
}

static void _mal_player_update_gain(mal_player *player) {
    if (player && player->context && player->data.sl_volume) {
        float gain = 0;
        if (!player->context->mute && !player->mute) {
            gain = player->context->gain * player->gain;
        }
        if (gain <= 0) {
            (*player->data.sl_volume)->SetMute(player->data.sl_volume, SL_BOOLEAN_TRUE);
        } else {
            SLmillibel millibelVolume = (SLmillibel)roundf(2000 * log10f(gain));
            if (millibelVolume < SL_MILLIBEL_MIN) {
                millibelVolume = SL_MILLIBEL_MIN;
            } else if (millibelVolume > 0) {
                millibelVolume = 0;
            }
            (*player->data.sl_volume)->SetVolumeLevel(player->data.sl_volume, millibelVolume);
            (*player->data.sl_volume)->SetMute(player->data.sl_volume, SL_BOOLEAN_FALSE);
        }
    }
}

static bool _mal_player_init(mal_player *player) {
    // Do nothing
    return true;
}

static void _mal_player_dispose(mal_player *player) {
    if (player->data.sl_object) {
        (*player->data.sl_object)->Destroy(player->data.sl_object);
        player->data.sl_object = NULL;
        player->data.sl_buffer_queue = NULL;
        player->data.sl_play = NULL;
        player->data.sl_volume = NULL;
    }
}

static bool _mal_player_set_format(mal_player *player, mal_format format) {
    _mal_player_dispose(player);
    if (!player->context) {
        return false;
    }

    const int n = 1;
    const bool system_is_little_endian = *(char *)&n == 1;

    SLDataLocator_BufferQueue sl_buffer_queue = {
        .locatorType = SL_DATALOCATOR_BUFFERQUEUE,
        .numBuffers = 2
    };

    SLDataFormat_PCM sl_format = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = format.num_channels,
        .samplesPerSec = (SLuint32)(format.sample_rate * 1000),
        .bitsPerSample = (format.bit_depth == 8 ? SL_PCMSAMPLEFORMAT_FIXED_8 :
                          SL_PCMSAMPLEFORMAT_FIXED_16),
        .containerSize = (format.bit_depth == 8 ? SL_PCMSAMPLEFORMAT_FIXED_8 :
                          SL_PCMSAMPLEFORMAT_FIXED_16),
        .channelMask = (format.num_channels == 2 ?
                        (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER),
        .endianness = (system_is_little_endian ? SL_BYTEORDER_LITTLEENDIAN : SL_BYTEORDER_BIGENDIAN)
    };

    SLDataSource sl_data_source = {&sl_buffer_queue, &sl_format};
    SLDataLocator_OutputMix sl_output_mix = {SL_DATALOCATOR_OUTPUTMIX,
        player->context->data.sl_output_mix_object};
    SLDataSink sl_audio_sink = {&sl_output_mix, NULL};

    // Create the player
    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    SLresult result =
    (*player->context->data.sl_engine)->CreateAudioPlayer(player->context->data.sl_engine,
                                                          &player->data.sl_object, &sl_data_source,
                                                          &sl_audio_sink, 2, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        player->data.sl_object = NULL;
        return false;
    }

    // Realize the player
    result = (*player->data.sl_object)->Realize(player->data.sl_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        _mal_player_dispose(player);
        return false;
    }

    // Get the play interface
    result = (*player->data.sl_object)->GetInterface(player->data.sl_object, SL_IID_PLAY,
                                                     &player->data.sl_play);
    if (result != SL_RESULT_SUCCESS) {
        _mal_player_dispose(player);
        return false;
    }

    // Get buffer queue interface
    result = (*player->data.sl_object)->GetInterface(player->data.sl_object, SL_IID_BUFFERQUEUE,
                                                     &player->data.sl_buffer_queue);
    if (result != SL_RESULT_SUCCESS) {
        _mal_player_dispose(player);
        return false;
    }

    // Register buffer queue callback
    result = (*player->data.sl_buffer_queue)->RegisterCallback(player->data.sl_buffer_queue,
                                                               _mal_buffer_queue_callback, player);
    if (result != SL_RESULT_SUCCESS) {
        _mal_player_dispose(player);
        return false;
    }

    // Get the volume interface (optional)
    result = (*player->data.sl_object)->GetInterface(player->data.sl_object, SL_IID_VOLUME,
                                                     &player->data.sl_volume);
    if (result != SL_RESULT_SUCCESS) {
        player->data.sl_volume = NULL;
    }

    player->format = format;
    _mal_player_update_gain(player);
    return true;
}

static bool _mal_player_set_buffer(mal_player *player, const mal_buffer *buffer) {
    // Do nothing
    return true;
}

static void _mal_player_set_mute(mal_player *player, bool mute) {
    _mal_player_update_gain(player);
}

static void _mal_player_set_gain(mal_player *player, float gain) {
    _mal_player_update_gain(player);
}

static void _mal_player_set_looping(mal_player *player, bool looping) {
    // Do nothing
}

static mal_player_state _mal_player_get_state(const mal_player *player) {
    if (!player->data.sl_play) {
        return MAL_PLAYER_STATE_STOPPED;
    } else {
        SLuint32 state;
        SLresult result = (*player->data.sl_play)->GetPlayState(player->data.sl_play, &state);
        if (result != SL_RESULT_SUCCESS) {
            return MAL_PLAYER_STATE_STOPPED;
        } else if (state == SL_PLAYSTATE_PAUSED) {
            return MAL_PLAYER_STATE_PAUSED;
        } else if (state == SL_PLAYSTATE_PLAYING) {
            return MAL_PLAYER_STATE_PLAYING;
        } else {
            return MAL_PLAYER_STATE_STOPPED;
        }
    }
}

static bool _mal_player_set_state(mal_player *player, mal_player_state state) {
    SLuint32 sl_state;
    switch (state) {
        case MAL_PLAYER_STATE_STOPPED:
        default:
            sl_state = SL_PLAYSTATE_STOPPED;
            break;
        case MAL_PLAYER_STATE_PAUSED:
            sl_state = SL_PLAYSTATE_PAUSED;
            break;
        case MAL_PLAYER_STATE_PLAYING:
            sl_state = SL_PLAYSTATE_PLAYING;
            break;
    }

    // Queue if needed
    if (sl_state == SL_PLAYSTATE_PLAYING && player->data.sl_buffer_queue) {
        const mal_buffer *buffer = player->buffer;
        if (buffer->managed_data) {
            const size_t len = (buffer->num_frames * (buffer->format.bit_depth / 8) *
                                buffer->format.num_channels);
            (*player->data.sl_buffer_queue)->Enqueue(player->data.sl_buffer_queue,
                                                     buffer->managed_data, len);
        }
    }

    (*player->data.sl_play)->SetPlayState(player->data.sl_play, sl_state);

    // Clear buffer queue
    if (sl_state == SL_PLAYSTATE_STOPPED && player->data.sl_buffer_queue) {
        (*player->data.sl_buffer_queue)->Clear(player->data.sl_buffer_queue);
    }

    return true;
}

#endif