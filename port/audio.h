/*
 * DM40: sound is not implemented yet (stubs in stubs.c). Interface of the
 * MG24 port's audio.h.
 */
#ifndef SRC_PWM_AUDIO_H_
#define SRC_PWM_AUDIO_H_
#include <stdint.h>
#define AUDIO_SAMPLE_RATE               11025
#define MAX_CHANNELS                    (8)
#define AUDIO_BUFFER_LENGTH             (1024)
#define AUDIO_BUFFER_DELAY              (200)
#define DMX_DATA_SOUND_OFFSET           0x18
typedef struct
{
    uint16_t lastAudioBufferIdx;
    uint16_t offset;
    uint8_t sfxIdx;
    int8_t volumeLeft;
    int8_t volumeRight;
} soundChannel_t;
void initAudio();
void updateSound();
void muteSound();
extern soundChannel_t soundChannels[MAX_CHANNELS];
#endif
