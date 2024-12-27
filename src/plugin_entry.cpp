#include "plugin.h"
#include "utils.h"

#define GUI_API CLAP_WINDOW_API_WIN32


static constexpr clap_plugin_note_ports_t extensionNotePorts = {
    .count = [] (const clap_plugin_t *plugin, const bool isInput) -> uint32_t {
        return isInput ? 1 : 0;
    },
    .get = [] (const clap_plugin_t *plugin, const uint32_t index, const bool isInput, clap_note_port_info_t *info) -> bool {
        if (!isInput || index) return false;
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        snprintf(info->name, sizeof(info->name), "%s", "Note Port");
        return true;
    },
};

static constexpr clap_plugin_audio_ports_t extensionAudioPorts = {
    .count = [] (const clap_plugin_t *plugin, const bool isInput) -> uint32_t {
        return isInput ? 0 : 1;
    },
    .get = [] (const clap_plugin_t *plugin, const uint32_t index, const bool isInput, clap_audio_port_info_t *info) -> bool {
        if (isInput || index) return false;
        info->id = 0;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name), "%s", "Audio Output");
        return true;
    },
};

static constexpr clap_plugin_params_t extensionParams = {
    .count = [] (const clap_plugin_t *plugin) -> uint32_t {
        return P_COUNT;
    },

    .get_info = [] (const clap_plugin_t *_plugin, const uint32_t index, clap_param_info_t *information) -> bool {
        if (index == P_VOLUME) {
            memset(information, 0, sizeof(clap_param_info_t));
            information->id = index;
            // These flags enable polyphonic modulation.
            information->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID;
            information->min_value = 0.0f;
            information->max_value = 1.0f;
            information->default_value = 0.5f;
            strcpy(information->name, "Volume");
            return true;
        } else {
            return false;
        }
    },

    .get_value = [] (const clap_plugin_t *_plugin, clap_id id, double *value) -> bool {
        const auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        const auto i = (uint32_t) id;
        if (i >= P_COUNT) return false;

        // get_value is called on the main thread, but should return the value of the parameter according to the audio thread,
        // since the value on the audio thread is the one that host communicates with us via CLAP_EVENT_PARAM_VALUE events.
        // Since we're accessing the opposite thread's arrays, we must acquire the syncParameters mutex.
        // And although we need to check the mainChanged array, we mustn't actually modify the parameters array,
        // since that can only be done on the audio thread. Don't worry -- it'll pick up the changes eventually.
        MutexAcquire(plugin->syncParameters);
        *value = plugin->mainChanged[i] ? plugin->mainParameters[i] : plugin->parameters[i];
        MutexRelease(plugin->syncParameters);
        return true;
    },

    .value_to_text = [] (const clap_plugin_t *_plugin, const clap_id id, const double value, char *display, const uint32_t size) {
        if (const auto i = static_cast<uint32_t>(id); i >= P_COUNT) return false;
        snprintf(display, size, "%f", value);
        return true;
    },

    .text_to_value = [] (const clap_plugin_t *_plugin, clap_id param_id, const char *display, double *value) {
        // TODO Implement this.
        return false;
    },

    .flush = [] (const clap_plugin_t *_plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        const uint32_t eventCount = in->size(in);

        // For parameters that have been modified by the main thread, send CLAP_EVENT_PARAM_VALUE events to the host.
        PluginSyncMainToAudio(plugin, out);

        // Process events sent to our plugin from the host.
        for (uint32_t eventIndex = 0; eventIndex < eventCount; eventIndex++) {
            PluginProcessEvent(plugin, in->get(in, eventIndex));
        }
    },
};

static constexpr clap_plugin_state_t extensionState = {
    .save = [] (const clap_plugin_t *_plugin, const clap_ostream_t *stream) -> bool {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);

        // Synchronize any changes from the audio thread (that is, parameter values sent to us by the host)
        // before we save the state of the plugin.
        PluginSyncAudioToMain(plugin);

        return sizeof(float) * P_COUNT == stream->write(stream, plugin->mainParameters, sizeof(float) * P_COUNT);
    },

    .load = [] (const clap_plugin_t *_plugin, const clap_istream_t *stream) -> bool {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);

        // Since we're modifying a parameter array, we need to acquire the syncParameters mutex.
        MutexAcquire(plugin->syncParameters);
        const bool success = sizeof(float) * P_COUNT == stream->read(stream, plugin->mainParameters, sizeof(float) * P_COUNT);
        // Make sure that the audio thread will pick up upon the modified parameters next time pluginClass.process is called.
        for (bool & i : plugin->mainChanged) i = true;
        MutexRelease(plugin->syncParameters);

        return success;
    },
};

static constexpr clap_plugin_gui_t extensionGUI = {
    .is_api_supported = [] (const clap_plugin_t *plugin, const char *api, bool isFloating) -> bool {
        return 0 == strcmp(api, GUI_API) && !isFloating;
    },

    .get_preferred_api = [] (const clap_plugin_t *plugin, const char **api, bool *isFloating) -> bool {
        *api = GUI_API;
        *isFloating = false;
        return true;
    },

    .create = [] (const clap_plugin_t *_plugin, const char *api, bool isFloating) -> bool {
        if (!extensionGUI.is_api_supported(_plugin, api, isFloating)) return false;
        GUICreate(static_cast<MyPlugin *>(_plugin->plugin_data));
        return true;
    },

    .destroy = [] (const clap_plugin_t *_plugin) {
        GUIDestroy(static_cast<MyPlugin *>(_plugin->plugin_data));
    },

    .set_scale = [] (const clap_plugin_t *plugin, double scale) -> bool {
        return false;
    },

    .get_size = [] (const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) -> bool {
        *width = GUI_WIDTH;
        *height = GUI_HEIGHT;
        return true;
    },

    .can_resize = [] (const clap_plugin_t *plugin) -> bool {
        return false;
    },

    .get_resize_hints = [] (const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints) -> bool {
        return false;
    },

    .adjust_size = [] (const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) -> bool {
        return extensionGUI.get_size(plugin, width, height);
    },

    .set_size = [] (const clap_plugin_t *plugin, uint32_t width, uint32_t height) -> bool {
        return true;
    },

    .set_parent = [] (const clap_plugin_t *_plugin, const clap_window_t *window) -> bool {
        assert(0 == strcmp(window->api, GUI_API));
        GUISetParent(static_cast<MyPlugin *>(_plugin->plugin_data), window);
        return true;
    },

    .set_transient = [] (const clap_plugin_t *plugin, const clap_window_t *window) -> bool {
        return false;
    },

    .suggest_title = [] (const clap_plugin_t *plugin, const char *title) {
    },

    .show = [] (const clap_plugin_t *_plugin) -> bool {
        GUISetVisible(static_cast<MyPlugin *>(_plugin->plugin_data), true);
        return true;
    },

    .hide = [] (const clap_plugin_t *_plugin) -> bool {
        GUISetVisible(static_cast<MyPlugin *>(_plugin->plugin_data), false);
        return true;
    },
};

static constexpr clap_plugin_posix_fd_support_t extensionPOSIXFDSupport = {
    .on_fd = [] (const clap_plugin_t *_plugin, int fd, clap_posix_fd_flags_t flags) {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        GUIOnPOSIXFD(plugin);
    },
};

static constexpr clap_plugin_timer_support_t extensionTimerSupport = {
    .on_timer = [] (const clap_plugin_t *_plugin, clap_id timerID) {
        // If the GUI is open and at least one parameter value has changed...
        if (auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data); plugin->gui && PluginSyncAudioToMain(plugin)) {
            // Repaint the GUI.
            GUIPaint(plugin, true);
        }
    },
};


static clap_plugin_t pluginClass = {
    .desc = &pluginDescriptor,
    .plugin_data = nullptr,

    .init = [] (const clap_plugin *_plugin) -> bool {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);

        plugin->hostPOSIXFDSupport = static_cast<const clap_host_posix_fd_support_t *>(plugin->host->get_extension(plugin->host, CLAP_EXT_POSIX_FD_SUPPORT));
        MutexInitialise(plugin->syncParameters);

        plugin->hostParams = static_cast<const clap_host_params_t *>(plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));

        for (uint32_t i = 0; i < P_COUNT; i++) {
            clap_param_info_t information = {};
            extensionParams.get_info(_plugin, i, &information);
            plugin->mainParameters[i] = plugin->parameters[i] = information.default_value;
        }

        plugin->hostTimerSupport = static_cast<const clap_host_timer_support_t *>(plugin->host->get_extension(plugin->host, CLAP_EXT_TIMER_SUPPORT));

        if (plugin->hostTimerSupport && plugin->hostTimerSupport->register_timer) {
            plugin->hostTimerSupport->register_timer(plugin->host, 200 /* every 200 milliseconds */, &plugin->timerID);
        }

        return true;
    },

    .destroy = [] (const clap_plugin *_plugin) {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        plugin->voices.Free();
        if (plugin->hostTimerSupport && plugin->hostTimerSupport->register_timer) {
            plugin->hostTimerSupport->unregister_timer(plugin->host, plugin->timerID);
        }
        MutexDestroy(plugin->syncParameters);
        free(plugin);
    },

    .activate = [] (const clap_plugin *_plugin, const double sampleRate, uint32_t minimumFramesCount, uint32_t maximumFramesCount) -> bool {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        plugin->sampleRate = sampleRate;
        return true;
    },

    .deactivate = [] (const clap_plugin *_plugin) {
    },

    .start_processing = [] (const clap_plugin *_plugin) -> bool {
        return true;
    },

    .stop_processing = [] (const clap_plugin *_plugin) {
    },

    .reset = [] (const clap_plugin *_plugin) {
        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);
        plugin->voices.Free();
    },

    .process = [] (const clap_plugin *_plugin, const clap_process_t *process) -> clap_process_status {

        auto *plugin = static_cast<MyPlugin *>(_plugin->plugin_data);

        PluginSyncMainToAudio(plugin, process->out_events);

        assert(process->audio_outputs_count == 1);
        assert(process->audio_inputs_count == 0);

        const uint32_t frameCount = process->frames_count;
        const uint32_t inputEventCount = process->in_events->size(process->in_events);
        uint32_t eventIndex = 0;
        uint32_t nextEventFrame = inputEventCount ? 0 : frameCount;

        for (uint32_t i = 0; i < frameCount; ) {
            while (eventIndex < inputEventCount && nextEventFrame == i) {
                const clap_event_header_t *event = process->in_events->get(process->in_events, eventIndex);

                if (event->time != i) {
                    nextEventFrame = event->time;
                    break;
                }

                PluginProcessEvent(plugin, event);
                eventIndex++;

                if (eventIndex == inputEventCount) {
                    nextEventFrame = frameCount;
                    break;
                }
            }

            PluginRenderAudio(plugin, i, nextEventFrame, process->audio_outputs[0].data32[0], process->audio_outputs[0].data32[1]);
            i = nextEventFrame;
        }

        for (int i = 0; i < plugin->voices.Length(); i++) {
            if (const Voice *voice = &plugin->voices[i]; !voice->held) {
                clap_event_note_t event = {};
                event.header.size = sizeof(event);
                event.header.time = 0;
                event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                event.header.type = CLAP_EVENT_NOTE_END;
                event.header.flags = 0;
                event.key = voice->key;
                event.note_id = voice->noteID;
                event.channel = voice->channel;
                event.port_index = 0;
                process->out_events->try_push(process->out_events, &event.header);

                plugin->voices.Delete(i--);
            }
        }

        return CLAP_PROCESS_CONTINUE;
    },

    .get_extension = [] (const clap_plugin *plugin, const char *id) -> const void * {
        if (0 == strcmp(id, CLAP_EXT_NOTE_PORTS )) return &extensionNotePorts;
        if (0 == strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &extensionAudioPorts;
        if (0 == strcmp(id, CLAP_EXT_PARAMS     )) return &extensionParams;
        if (0 == strcmp(id, CLAP_EXT_STATE      )) return &extensionState;
        if (0 == strcmp(id, CLAP_EXT_GUI             )) return &extensionGUI;
        if (0 == strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &extensionPOSIXFDSupport;
        if (0 == strcmp(id, CLAP_EXT_TIMER_SUPPORT   )) return &extensionTimerSupport;
        if (0 == strcmp(id, CLAP_EXT_STATE           )) return &extensionState;
        return nullptr;
    },

    .on_main_thread = [] (const clap_plugin *_plugin) {
    },
};



static constexpr clap_plugin_factory_t pluginFactory = {
    .get_plugin_count = [] (const clap_plugin_factory *factory) -> uint32_t {
        return 1;
    },

    .get_plugin_descriptor = [] (const clap_plugin_factory *factory, const uint32_t index) -> const clap_plugin_descriptor_t * {
        return index == 0 ? &pluginDescriptor : nullptr;
    },

    .create_plugin = [] (const clap_plugin_factory *factory, const clap_host_t *host, const char *pluginID) -> const clap_plugin_t * {
        if (!clap_version_is_compatible(host->clap_version) || strcmp(pluginID, pluginDescriptor.id) != 0) {
            return nullptr;
        }

        auto *plugin = static_cast<MyPlugin *>(calloc(1, sizeof(MyPlugin)));
        plugin->host = host;
        plugin->plugin = pluginClass;
        plugin->plugin.plugin_data = plugin;
        return &plugin->plugin;
    },
};

extern "C" const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,

    .init = [] (const char *path) -> bool {
        return true;
    },

    .deinit = [] () {},

    .get_factory = [] (const char *factoryID) -> const void * {
        return strcmp(factoryID, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &pluginFactory;
    },
};