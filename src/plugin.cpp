#include "plugin.h"

template <class T>
void Array<T>::Insert(T newItem, uintptr_t index) {
    if (length + 1 > allocated) {
        allocated *= 2;
        if (length + 1 > allocated) allocated = length + 1;
        array = static_cast<T *>(realloc(array, allocated * sizeof(T)));
    }

    length++;
    memmove(array + index + 1, array + index, (length - index - 1) * sizeof(T));
    array[index] = newItem;
}

template <class T>
void Array<T>::Delete(uintptr_t index) {
    memmove(array + index, array + index + 1, (length - index - 1) * sizeof(T));
    length--;
}

template <class T>
void Array<T>::Add(T item) { Insert(item, length); }

template <class T>
void Array<T>::Free() { free(array); array = nullptr; length = allocated = 0; }

template <class T>
int Array<T>::Length() const { return length; }

template <class T>
T &Array<T>::operator[](uintptr_t index) { assert(index < length); return array[index]; }

const clap_plugin_descriptor_t pluginDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "joeloftus.HelloCLAP",
    .name = "HelloCLAP",
    .vendor = "joeloftus",
    .version = "1.0.0",
    .features = (const char *[]) {
        CLAP_PLUGIN_FEATURE_INSTRUMENT,
        CLAP_PLUGIN_FEATURE_SYNTHESIZER,
        CLAP_PLUGIN_FEATURE_STEREO,
        nullptr,
    },
};

void PluginRenderAudio(MyPlugin *plugin, uint32_t start, uint32_t end, float *outputL, float *outputR) {
    for (uint32_t index = start; index < end; index++) {
        float sum = 0.0f;

        for (int i = 0; i < plugin->voices.Length(); i++) {
            Voice *voice = &plugin->voices[i];
            if (!voice->held) continue;

            // New!
            const float volume = FloatClamp01(plugin->parameters[P_VOLUME] + voice->parameterOffsets[P_VOLUME]);
            sum += sinf(voice->phase * 2.0f * 3.14159f) * 0.2f * volume;

            voice->phase += 440.0f * exp2f((voice->key - 57.0f) / 12.0f) / plugin->sampleRate;
            voice->phase -= floorf(voice->phase);
        }

        outputL[index] = sum;
        outputR[index] = sum;
    }
}

void PluginProcessEvent(MyPlugin *plugin, const clap_event_header_t *event) {
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        if (event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto *noteEvent = reinterpret_cast<const clap_event_note_t *>(event);

            for (int i = 0; i < plugin->voices.Length(); i++) {
                if (Voice *voice = &plugin->voices[i]; (noteEvent->key == -1 || voice->key == noteEvent->key)
                                                       && (noteEvent->note_id == -1 || voice->noteID == noteEvent->note_id)
                                                       && (noteEvent->channel == -1 || voice->channel == noteEvent->channel)) {
                    if (event->type == CLAP_EVENT_NOTE_CHOKE) {
                        plugin->voices.Delete(i--);
                    } else {
                        voice->held = false;
                    }
                }
            }

            if (event->type == CLAP_EVENT_NOTE_ON) {
                Voice voice = {
                    .held = true,
                    .noteID = noteEvent->note_id,
                    .channel = noteEvent->channel,
                    .key = noteEvent->key,
                    .phase = 0.0f,
                };

                plugin->voices.Add(voice);
            }
        }
    }
    if (event->type == CLAP_EVENT_PARAM_VALUE) {
        const auto *valueEvent = reinterpret_cast<const clap_event_param_value_t *>(event);
        const auto i = static_cast<uint32_t>(valueEvent->param_id);
        MutexAcquire(plugin->syncParameters);
        plugin->parameters[i] = valueEvent->value;
        plugin->changed[i] = true;
        MutexRelease(plugin->syncParameters);
    }

    if (event->type == CLAP_EVENT_PARAM_MOD) {
        const auto *modEvent = reinterpret_cast<const clap_event_param_mod_t *>(event);

        for (int i = 0; i < plugin->voices.Length(); i++) {
            if (Voice *voice = &plugin->voices[i]; (modEvent->key == -1 || voice->key == modEvent->key)
                                                   && (modEvent->note_id == -1 || voice->noteID == modEvent->note_id)
                                                   && (modEvent->channel == -1 || voice->channel == modEvent->channel)) {
                voice->parameterOffsets[modEvent->param_id] = modEvent->amount;
                break;
                    }
        }
    }
}

void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out) {
    MutexAcquire(plugin->syncParameters);

    for (uint32_t i = 0; i < P_COUNT; i++) {
        if (plugin->gestureStart[i]) {
            plugin->gestureStart[i] = false;

            clap_event_param_gesture_t event = {};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
            event.header.flags = 0;
            event.param_id = i;
            out->try_push(out, &event.header);
        }

        if (plugin->mainChanged[i]) {
            plugin->parameters[i] = plugin->mainParameters[i];
            plugin->mainChanged[i] = false;

            clap_event_param_value_t event = {};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = 0;
            event.param_id = i;
            event.cookie = nullptr;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plugin->parameters[i];
            out->try_push(out, &event.header);
        }

        if (plugin->gestureEnd[i]) {
            plugin->gestureEnd[i] = false;

            clap_event_param_gesture_t event = {};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_GESTURE_END;
            event.header.flags = 0;
            event.param_id = i;
            out->try_push(out, &event.header);
        }
    }

    MutexRelease(plugin->syncParameters);
}

bool PluginSyncAudioToMain(MyPlugin *plugin) {
    bool anyChanged = false;
    MutexAcquire(plugin->syncParameters);

    for (uint32_t i = 0; i < P_COUNT; i++) {
        if (plugin->changed[i]) {
            plugin->mainParameters[i] = plugin->parameters[i];
            plugin->changed[i] = false;
            anyChanged = true;
        }
    }

    MutexRelease(plugin->syncParameters);
    return anyChanged;
}
void PluginPaintRectangle(MyPlugin *plugin, uint32_t *bits, uint32_t l, uint32_t r, uint32_t t, uint32_t b, uint32_t border, uint32_t fill) {
    for (uint32_t i = t; i < b; i++) {
        for (uint32_t j = l; j < r; j++) {
            bits[i * GUI_WIDTH + j] = (i == t || i == b - 1 || j == l || j == r - 1) ? border : fill;
        }
    }
}


void PluginPaint(MyPlugin *plugin, uint32_t *bits) {
    // Draw the background.
    PluginPaintRectangle(plugin, bits, 0, GUI_WIDTH, 0, GUI_HEIGHT, 0xC0C0C0, 0xC0C0C0);

    // Draw the parameter, using the parameter value owned by the main thread.
    PluginPaintRectangle(plugin, bits, 10, 40, 10, 40, 0x000000, 0xC0C0C0);
    PluginPaintRectangle(plugin, bits, 10, 40, 10 + 30 * (1.0f - plugin->mainParameters[P_VOLUME]), 40, 0x000000, 0x000000);
}

void PluginProcessMouseDrag(MyPlugin *plugin, int32_t x, int32_t y) {
    if (plugin->mouseDragging) {
        // Compute the new value of the parameter based on the mouse's position.
        const float newValue = FloatClamp01(plugin->mouseDragOriginValue + (plugin->mouseDragOriginY - y) * 0.01f);

        // Under the syncParameters mutex, update the main thread's parameters array,
        // and inform the audio thread it should read the value into its array.
        MutexAcquire(plugin->syncParameters);
        plugin->mainParameters[plugin->mouseDraggingParameter] = newValue;
        plugin->mainChanged[plugin->mouseDraggingParameter] = true;
        MutexRelease(plugin->syncParameters);

        // As before.
        if (plugin->hostParams && plugin->hostParams->request_flush) {
            plugin->hostParams->request_flush(plugin->host);
        }
    }
}

void PluginProcessMousePress(MyPlugin *plugin, int32_t x, int32_t y) {
    // If the cursor is inside the dial...
    if (x >= 10 && x < 40 && y >= 10 && y < 40) {
        // Start dragging.
        plugin->mouseDragging = true;
        plugin->mouseDraggingParameter = P_VOLUME;
        plugin->mouseDragOriginX = x;
        plugin->mouseDragOriginY = y;
        plugin->mouseDragOriginValue = plugin->mainParameters[P_VOLUME];

        // Inform the audio thread to send a gesture start event.
        MutexAcquire(plugin->syncParameters);
        plugin->gestureStart[plugin->mouseDraggingParameter] = true;
        MutexRelease(plugin->syncParameters);

        if (plugin->hostParams && plugin->hostParams->request_flush) {
            plugin->hostParams->request_flush(plugin->host);
        }
    }
}

void PluginProcessMouseRelease(MyPlugin *plugin) {
    if (plugin->mouseDragging) {
        // Inform the audio thread to send a gesture end event.
        MutexAcquire(plugin->syncParameters);
        plugin->gestureEnd[plugin->mouseDraggingParameter] = true;
        MutexRelease(plugin->syncParameters);

        // As before.
        if (plugin->hostParams && plugin->hostParams->request_flush) {
            plugin->hostParams->request_flush(plugin->host);
        }

        // Dragging has stopped.
        plugin->mouseDragging = false;
    }
}

// Explicitly instantiate the template for Voice
template class Array<Voice>;