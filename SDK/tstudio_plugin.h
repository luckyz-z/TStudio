#ifndef TSTUDIO_PLUGIN_H
#define TSTUDIO_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define TSTUDIO_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#define TSTUDIO_EXPORT __declspec(dllexport)
#else
#define TSTUDIO_EXPORT __attribute__((visibility("default")))
#endif

typedef struct tstudio_param
{
    const char* id;
    const char* name;
    const char* unit;
    float min;
    float max;
    float default_value;
    int num_choices;
    const char* const* choices;
} tstudio_param;

enum tstudio_event_type
{
    TSTUDIO_NOTE_ON = 1,
    TSTUDIO_NOTE_OFF = 2
};

typedef struct tstudio_event
{
    int type;
    int offset;
    int key;
    float velocity;
} tstudio_event;

typedef struct tstudio_transport
{
    double bpm;
    double beat;
    int playing;
} tstudio_transport;

typedef struct tstudio_plugin
{
    int api_version;
    const char* name;
    const char* author;
    const char* description;
    int is_instrument;
    int num_params;
    const tstudio_param* params;

    void* (*create)(double sample_rate, int max_block_size);
    void (*destroy)(void* instance);
    void (*set_param)(void* instance, int index, float value);
    void (*process)(void* instance, float* left, float* right, int frames, const tstudio_event* events, int num_events, const tstudio_transport* transport);
} tstudio_plugin;

typedef const tstudio_plugin* const* (*tstudio_entry_fn)(int* count);

TSTUDIO_EXPORT const tstudio_plugin* const* tstudio_plugins(int* count);

#ifdef __cplusplus
}
#endif

#endif
