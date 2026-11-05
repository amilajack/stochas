// SPDX-License-Identifier: GPL-3.0-only
// Matched-source CLAP oracle. Never creates an editor or installs the plug-in.
#include <clap/clap.h>
#include <clap/ext/thread-check.h>
#include <dlfcn.h>
#include <array>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" bool stochas_oracle_background_only();
extern "C" bool stochas_oracle_never_frontmost();
static bool audioThread = false;
static const clap_host_thread_check_t threadCheck{
    [](const clap_host_t*) { return !audioThread; },
    [](const clap_host_t*) { return audioThread; }};
static const void* extension(const clap_host_t*, const char* id) {
    return std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0 ? &threadCheck : nullptr;
}
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct State {
    std::vector<uint8_t> bytes;
    size_t cursor = 0;
    clap_ostream_t output{this, [](const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t {
        auto& v = static_cast<State*>(s->ctx)->bytes;
        const auto* begin = static_cast<const uint8_t*>(data);
        v.insert(v.end(), begin, begin + size); return static_cast<int64_t>(size);
    }};
    clap_istream_t input{this, [](const clap_istream_t* s, void* data, uint64_t size) -> int64_t {
        auto& state = *static_cast<State*>(s->ctx);
        const size_t count = std::min<size_t>(size, state.bytes.size() - state.cursor);
        std::memcpy(data, state.bytes.data() + state.cursor, count); state.cursor += count;
        return static_cast<int64_t>(count);
    }};
};
struct Capture {
    uint64_t absolute = 0;
    std::array<clap_event_midi_t, 32768> messages{};
    std::array<uint64_t, 32768> times{};
    size_t count = 0;
    clap_output_events_t output{this, [](const clap_output_events_t* list, const clap_event_header_t* event) {
        auto& c = *static_cast<Capture*>(list->ctx);
        if (event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_MIDI) return true;
        if (c.count == c.messages.size()) return false;
        c.messages[c.count] = *reinterpret_cast<const clap_event_midi_t*>(event);
        c.times[c.count++] = c.absolute + event->time; return true;
    }};
};
int main(int argc, char** argv) {
    try {
        require(stochas_oracle_background_only(), "background-only activation policy unavailable");
        require(argc == 4, "usage: native-oracle PLUGIN STATE_OR_- OUTPUT_JSON");
        const char* config = std::getenv("STOCHAS_CONFIG_ROOT");
        require(config && std::filesystem::path(config).is_absolute(), "isolated STOCHAS_CONFIG_ROOT required");
        std::filesystem::create_directories(config);
        double beatOffset = 0;
        if (const char *offset = std::getenv("STOCHAS_ORACLE_BEAT_OFFSET")) {
            char *end = nullptr; beatOffset = std::strtod(offset,&end);
            require(end && !*end && std::isfinite(beatOffset), "invalid oracle beat offset");
        }
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error(dlerror());
        auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        require(entry && entry->init(argv[1]), "CLAP entry init failed");
        auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        require(factory && factory->get_plugin_count(factory) == 1, "expected one Stochas descriptor");
        const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
        require(std::strcmp(descriptor->id, "org.surge-synth-team.stochas") == 0, "unexpected plugin id");
        clap_host_t host{CLAP_VERSION, nullptr, "Stochas matched oracle", "Surge Synth Team", "https://github.com/amilajack/stochas", "1", extension,
            [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
        const clap_plugin_t* plugin = factory->create_plugin(factory, &host, descriptor->id);
        require(plugin && plugin->init(plugin), "plugin init failed");
        auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        require(state, "state extension missing");
        if (std::strcmp(argv[2], "-") != 0) {
            std::ifstream input(argv[2], std::ios::binary);
            require(input.good(), "state fixture missing");
            State fixture; fixture.bytes.assign(std::istreambuf_iterator<char>(input), {});
            require(state->load(plugin, &fixture.input), "state fixture load failed");
        }
        State saved; require(state->save(plugin, &saved.output) && !saved.bytes.empty(), "state save failed");
        {
            std::ofstream stateFile(std::string(argv[3]) + ".state", std::ios::binary);
            stateFile.write(reinterpret_cast<const char*>(saved.bytes.data()), saved.bytes.size());
        }
        std::vector<clap_param_info_t> parameterInfo;
        auto* parameters = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        require(parameters, "parameters extension missing");
        for (uint32_t index = 0; index < parameters->count(plugin); ++index) {
            clap_param_info_t info{};
            require(parameters->get_info(plugin, index, &info), "parameter metadata failed");
            parameterInfo.push_back(info);
        }
        saved.cursor = 0;
        require(state->load(plugin,&saved.input), "canonical saved state reload failed");
        State roundtrip;
        require(state->save(plugin,&roundtrip.output), "canonical state resave failed");
        if (roundtrip.bytes != saved.bytes) {
            std::ofstream diagnostic(std::string(argv[3])+".roundtrip.state",std::ios::binary);
            diagnostic.write(reinterpret_cast<const char*>(roundtrip.bytes.data()),roundtrip.bytes.size());
            throw std::runtime_error("canonical state roundtrip changed bytes");
        }
        auto* latency = static_cast<const clap_plugin_latency_t*>(plugin->get_extension(plugin,CLAP_EXT_LATENCY));
        auto* notes = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        require(notes && notes->count(plugin, true) == 1 && notes->count(plugin, false) == 1, "expected MIDI input and output");
        auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        const uint32_t inputCount = audioPorts ? audioPorts->count(plugin, true) : 0;
        const uint32_t outputCount = audioPorts ? audioPorts->count(plugin, false) : 0;
        std::vector<clap_audio_buffer_t> inputs(inputCount), outputs(outputCount);
        std::vector<std::vector<float>> samples;
        std::vector<std::vector<float*>> channelPointers;
        std::vector<uint32_t> channelCounts;
        for (bool isInput : {true, false}) {
            const uint32_t count = isInput ? inputCount : outputCount;
            for (uint32_t index = 0; index < count; ++index) {
                clap_audio_port_info_t info{};
                require(audioPorts->get(plugin, index, isInput, &info), "audio port metadata failed");
                require(info.channel_count <= 64, "unreasonable oracle audio channel count");
                channelCounts.push_back(info.channel_count);
                channelPointers.emplace_back();
                auto& pointers = channelPointers.back();
                for (uint32_t channel = 0; channel < info.channel_count; ++channel) {
                    samples.emplace_back(512, 0.0f); pointers.push_back(samples.back().data());
                }
                auto& port = isInput ? inputs[index] : outputs[index];
                port.channel_count = info.channel_count; port.data32 = pointers.data();
            }
        }
        require(plugin->activate(plugin, 48000, 1, 512), "activation failed");
        const uint32_t latencyFrames = latency ? latency->get(plugin) : 0;
        audioThread = true; require(plugin->start_processing(plugin), "start processing failed");
        Capture capture;
        require(stochas_oracle_never_frontmost(), "oracle unexpectedly frontmost");
        clap_input_events_t input{nullptr, [](const clap_input_events_t*) -> uint32_t { return 0; },
            [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; }};
        clap_event_transport_t transport{};
        transport.header = {sizeof(transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
        transport.tempo = 120; transport.tsig_num = 4; transport.tsig_denom = 4;
        for (uint64_t frame = 0; frame < 192000; frame += 128) {
            capture.absolute = frame;
            transport.song_pos_beats = static_cast<clap_beattime>((static_cast<double>(frame) / 24000.0 + beatOffset) * CLAP_BEATTIME_FACTOR);
            clap_process_t process{}; process.steady_time = static_cast<int64_t>(frame); process.frames_count = 128;
            process.transport = &transport; process.in_events = &input; process.out_events = &capture.output;
            process.audio_inputs = inputs.empty() ? nullptr : inputs.data(); process.audio_inputs_count = inputCount;
            process.audio_outputs = outputs.empty() ? nullptr : outputs.data(); process.audio_outputs_count = outputCount;
            require(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR, "process failed");
            for (const auto &port : outputs)
                for (uint32_t channel = 0; channel < port.channel_count; ++channel)
                    for (uint32_t sample = 0; sample < process.frames_count; ++sample)
                        require(port.data32[channel][sample] == 0, "native note effect produced audio");
        }
        require(stochas_oracle_never_frontmost(), "oracle unexpectedly frontmost");
        plugin->stop_processing(plugin); audioThread = false; plugin->deactivate(plugin);
        std::ofstream report(argv[3]);
        report << std::setprecision(17);
        report << "{\"plugin\":\"" << descriptor->id << "\",\"sampleRate\":48000,\"frames\":192000,\"audioInputPorts\":" << inputCount
               << ",\"audioOutputPorts\":" << outputCount << ",\"latencyFrames\":" << latencyFrames << ",\"beatOffset\":" << beatOffset << ",\"stateBytes\":" << saved.bytes.size() << ",\"midi\":[";
        for (size_t i = 0; i < capture.count; ++i) {
            const auto& event = capture.messages[i]; if (i) report << ',';
            report << '[' << capture.times[i] << ',' << unsigned(event.data[0]) << ',' << unsigned(event.data[1]) << ',' << unsigned(event.data[2]) << ']';
        }
        report << "],\"parameters\":[";
        for (size_t index = 0; index < parameterInfo.size(); ++index) {
            const auto& info = parameterInfo[index]; if (index) report << ',';
            char defaultText[256]{};
            require(parameters->value_to_text(plugin, info.id, info.default_value, defaultText, sizeof(defaultText)), "parameter text conversion failed");
            report << "{\"id\":" << info.id << ",\"name\":\"" << info.name
                   << "\",\"module\":\"" << info.module << "\",\"min\":" << info.min_value
                   << ",\"max\":" << info.max_value << ",\"default\":" << info.default_value
                   << ",\"flags\":" << info.flags << ",\"defaultText\":\"" << defaultText << "\",\"textQueries\":[";
            std::vector<std::string> labels;
            const std::string name = info.name;
            if (name.find("pattern") != std::string::npos) labels = {"as assigned", "1", "2", "3", "4", "5", "6", "7", "8"};
            else if (name.find("muted") != std::string::npos) labels = {"as assigned", "no", "yes"};
            else if (name == "swing") labels = {"as assigned", "-50%", "0%", "25%", "50%"};
            else if (name == "playback") labels = {"manual", "stop", "play"};
            for (size_t query = 0; query < labels.size(); ++query) {
                double value = 0;
                require(parameters->text_to_value(plugin, info.id, labels[query].c_str(), &value), "parameter text query failed");
                char roundtrip[256]{};
                require(parameters->value_to_text(plugin, info.id, value, roundtrip, sizeof(roundtrip)), "parameter query readback failed");
                if (query) report << ',';
                report << "{\"text\":\"" << labels[query] << "\",\"value\":" << value << ",\"readback\":\"" << roundtrip << "\"}";
            }
            report << "]}";
        }
        report << "]}\n"; report.close();
        plugin->destroy(plugin); entry->deinit(); dlclose(library);
        std::cout << "PASS native CLAP state/process oracle; MIDI events=" << capture.count << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
