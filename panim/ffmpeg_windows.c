#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#include <raylib.h>

#include "ffmpeg.h"

#define READ_END 0
#define WRITE_END 1

struct FFMPEG {
    char dummy;
};

FFMPEG *ffmpeg_start_rendering_video(const char *output_path, size_t width, size_t height, size_t fps)
{
    (void)output_path;
    (void)width;
    (void)height;
    (void)fps;
    return NULL;
}

FFMPEG *ffmpeg_start_rendering_audio(const char *output_path)
{
    (void)output_path;
    return NULL;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    (void)ffmpeg;
    (void) cancel;
    return false;
}

bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height)
{
    (void)ffmpeg;
    (void)data;
    (void)width;
    (void)height;
    return false;
}

bool ffmpeg_send_sound_samples(FFMPEG *ffmpeg, void *data, size_t size)
{
    (void)ffmpeg;
    (void)data;
    (void)size;
    return false;
}
