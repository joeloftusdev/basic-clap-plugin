#pragma once

#include "clap/clap.h"
#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "parameters.h"
#include "utils.h"


#define GUI_WIDTH (300)
#define GUI_HEIGHT (200)

template <class T>
struct Array {
    T *array;
    size_t length, allocated;

    void Insert(T newItem, uintptr_t index);
    void Delete(uintptr_t index);
    void Add(T item);
    void Free();
    [[nodiscard]] int Length() const;
    T &operator[](uintptr_t index);
};

struct Voice {
    bool held;
    int32_t noteID;
    int16_t channel, key;
    float phase;
    float parameterOffsets[P_COUNT];
};

struct MyPlugin {
    clap_plugin_t plugin;
    const clap_host_t *host;
    float sampleRate;
    Array<Voice> voices;
    float parameters[P_COUNT], mainParameters[P_COUNT];
    bool changed[P_COUNT], mainChanged[P_COUNT];
    Mutex syncParameters;
    struct GUI *gui;
    const clap_host_posix_fd_support_t *hostPOSIXFDSupport;
    const clap_host_params_t *hostParams;
    bool gestureStart[P_COUNT], gestureEnd[P_COUNT];
    bool mouseDragging;
    uint32_t mouseDraggingParameter;
    int32_t mouseDragOriginX, mouseDragOriginY;
    float mouseDragOriginValue;
    const clap_host_timer_support_t *hostTimerSupport;
    clap_id timerID;
};

extern const clap_plugin_descriptor_t pluginDescriptor;
void PluginRenderAudio(MyPlugin *plugin, uint32_t start, uint32_t end, float *outputL, float *outputR);
void PluginProcessEvent(MyPlugin *plugin, const clap_event_header_t *event);
void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out);
bool PluginSyncAudioToMain(MyPlugin *plugin);
void PluginPaint(MyPlugin *plugin, uint32_t *bits);
void PluginProcessMousePress(MyPlugin *plugin, int x, int y);
void PluginProcessMouseDrag(MyPlugin *plugin, int x, int y);
void PluginProcessMouseRelease(MyPlugin *plugin);
void PluginPaintRectangle(MyPlugin *plugin, uint32_t *bits, uint32_t l, uint32_t r, uint32_t t, uint32_t b, uint32_t border, uint32_t fill);

void GUICreate(MyPlugin* plugin);
void GUIDestroy(MyPlugin* plugin);
void GUISetParent(const MyPlugin* plugin, const clap_window_t* window);
void GUISetVisible(const MyPlugin* plugin, bool visible);
void GUIOnPOSIXFD(MyPlugin* plugin);
void GUIPaint(MyPlugin* plugin, bool internal);

