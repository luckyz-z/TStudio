#include "../tstudio_plugin.h"

#include <math.h>
#include <stdlib.h>

#define TWO_PI 6.283185307179586

static const char* const crush_modes[] = { "Bits + rate", "Bits only", "Rate only" };

static const tstudio_param crusher_params[] = {
    { "bits", "Bits", "", 2.0f, 16.0f, 8.0f, 0, 0 },
    { "rate", "Downsample", "x", 1.0f, 32.0f, 4.0f, 0, 0 },
    { "mode", "Mode", "", 0.0f, 2.0f, 0.0f, 3, crush_modes },
    { "mix", "Mix", "", 0.0f, 1.0f, 1.0f, 0, 0 },
};

typedef struct crusher
{
    float bits, rate, mix;
    int mode;
    float held_l, held_r, counter;
} crusher;

static void* crusher_create(double sample_rate, int max_block_size)
{
    crusher* c = (crusher*)calloc(1, sizeof(crusher));
    (void)sample_rate;
    (void)max_block_size;
    c->bits = 8.0f;
    c->rate = 4.0f;
    c->mix = 1.0f;
    return c;
}

static void crusher_set(void* instance, int index, float value)
{
    crusher* c = (crusher*)instance;
    if (index == 0) c->bits = value;
    else if (index == 1) c->rate = value;
    else if (index == 2) c->mode = (int)(value + 0.5f);
    else if (index == 3) c->mix = value;
}

static float quantize(float x, float bits)
{
    const float steps = powf(2.0f, bits - 1.0f);
    return floorf(x * steps + 0.5f) / steps;
}

static void crusher_process(void* instance, float* left, float* right, int frames, const tstudio_event* events, int num_events, const tstudio_transport* transport)
{
    crusher* c = (crusher*)instance;
    const int use_bits = c->mode != 2, use_rate = c->mode != 1;
    (void)events;
    (void)num_events;
    (void)transport;
    for (int i = 0; i < frames; ++i)
    {
        c->counter += 1.0f;
        if (!use_rate || c->counter >= c->rate)
        {
            c->counter = 0.0f;
            c->held_l = use_bits ? quantize(left[i], c->bits) : left[i];
            c->held_r = use_bits ? quantize(right[i], c->bits) : right[i];
        }
        left[i] += (c->held_l - left[i]) * c->mix;
        right[i] += (c->held_r - right[i]) * c->mix;
    }
}

static const tstudio_param sub_params[] = {
    { "drive", "Drive", "", 0.0f, 1.0f, 0.2f, 0, 0 },
    { "decay", "Decay", "s", 0.05f, 4.0f, 0.8f, 0, 0 },
    { "volume", "Volume", "", 0.0f, 1.0f, 0.7f, 0, 0 },
};

typedef struct sub
{
    double rate, phase, frequency;
    float drive, decay, volume, level, velocity;
    int key;
} sub;

static void* sub_create(double sample_rate, int max_block_size)
{
    sub* s = (sub*)calloc(1, sizeof(sub));
    (void)max_block_size;
    s->rate = sample_rate;
    s->drive = 0.2f;
    s->decay = 0.8f;
    s->volume = 0.7f;
    s->key = -1;
    return s;
}

static void sub_set(void* instance, int index, float value)
{
    sub* s = (sub*)instance;
    if (index == 0) s->drive = value;
    else if (index == 1) s->decay = value;
    else if (index == 2) s->volume = value;
}

static void sub_event(sub* s, const tstudio_event* e)
{
    if (e->type == TSTUDIO_NOTE_ON)
    {
        s->key = e->key;
        s->frequency = 440.0 * pow(2.0, (e->key - 69) / 12.0);
        s->velocity = e->velocity;
        s->level = 1.0f;
    }
    else if (e->type == TSTUDIO_NOTE_OFF && e->key == s->key)
        s->key = -1;
}

static void sub_process(void* instance, float* left, float* right, int frames, const tstudio_event* events, int num_events, const tstudio_transport* transport)
{
    sub* s = (sub*)instance;
    const float hold = expf(-1.0f / (float)(s->decay * s->rate));
    const float release = expf(-1.0f / (float)(0.03 * s->rate));
    const float gain = 1.0f + s->drive * 6.0f;
    int next = 0;
    (void)transport;
    for (int i = 0; i < frames; ++i)
    {
        while (next < num_events && events[next].offset <= i)
            sub_event(s, &events[next++]);
        s->level *= s->key >= 0 ? hold : release;
        s->phase += s->frequency / s->rate;
        if (s->phase >= 1.0)
            s->phase -= 1.0;
        const float tone = tanhf((float)sin(TWO_PI * s->phase) * gain) / tanhf(gain);
        const float out = tone * s->level * s->velocity * s->volume;
        left[i] = out;
        right[i] = out;
    }
}

static const tstudio_plugin crusher_plugin = {
    TSTUDIO_PLUGIN_API_VERSION, "Native Crusher", "TStudio", "Bit and sample-rate crusher written in C", 0,
    4, crusher_params, crusher_create, free, crusher_set, crusher_process,
};

static const tstudio_plugin sub_plugin = {
    TSTUDIO_PLUGIN_API_VERSION, "Native Sub", "TStudio", "Sine sub bass with drive, written in C", 1,
    3, sub_params, sub_create, free, sub_set, sub_process,
};

static const tstudio_plugin* const all_plugins[] = { &crusher_plugin, &sub_plugin };

TSTUDIO_EXPORT const tstudio_plugin* const* tstudio_plugins(int* count)
{
    *count = 2;
    return all_plugins;
}
