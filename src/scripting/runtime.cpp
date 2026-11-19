#include "neurogenic/scripting/runtime.hpp"
#include "neurogenic/timeline/edit.hpp"
#ifdef NEUROGENIC_HAS_LUA
#include <sol/sol.hpp>
#endif
#include <optional>
#include <stdexcept>
#include <tuple>

namespace neurogenic {
#ifdef NEUROGENIC_HAS_LUA
namespace {
thread_local std::size_t instruction_budget{};
void instruction_hook(lua_State *state, lua_Debug *) {
    constexpr std::size_t interval = 1000;
    if (instruction_budget <= interval)
        luaL_error(state, "script instruction limit exceeded");
    instruction_budget -= interval;
}
void require(bool result, const std::string &error) {
    if (!result)
        throw std::runtime_error(error);
}
template <class T>
T field_or(const sol::table &table, const char *key, T fallback) {
    sol::object value = table[key];
    return value.valid() && value.get_type() != sol::type::nil ? value.as<T>()
                                                               : fallback;
}
TriggerCondition lua_condition(const std::string &value) {
    if (value == "always")
        return TriggerCondition::always;
    if (value == "first")
        return TriggerCondition::first_loop;
    if (value == "not-first")
        return TriggerCondition::not_first_loop;
    if (value == "every-n")
        return TriggerCondition::every_nth_loop;
    if (value == "n-of-m")
        return TriggerCondition::n_of_m;
    if (value == "previous-fired")
        return TriggerCondition::previous_fired;
    if (value == "previous-not-fired")
        return TriggerCondition::previous_not_fired;
    if (value == "fill")
        return TriggerCondition::fill_active;
    if (value == "not-fill")
        return TriggerCondition::fill_inactive;
    throw std::runtime_error("unknown trigger condition");
}
void apply_trigger(MusicalEvent &event, const sol::table &note) {
    const auto probability = field_or(
        note, "probability", static_cast<double>(event.trigger_probability));
    const auto n =
        field_or(note, "condition_n", static_cast<int>(event.condition_n));
    const auto m =
        field_or(note, "condition_m", static_cast<int>(event.condition_m));
    const auto ratchets =
        field_or(note, "ratchets", static_cast<int>(event.ratchet_count));
    if (probability < 0 || probability > 1)
        throw std::runtime_error("probability must be 0..1");
    if (n < 1 || m < 1 || n > m || m > 65535)
        throw std::runtime_error(
            "condition cycles require 1 <= n <= m <= 65535");
    if (ratchets < 1 || ratchets > 8)
        throw std::runtime_error("ratchets must be 1..8");
    event.trigger_probability = static_cast<float>(probability);
    sol::object condition = note["condition"];
    if (condition.valid() && condition.get_type() != sol::type::nil)
        event.trigger_condition = lua_condition(condition.as<std::string>());
    event.condition_n = static_cast<std::uint16_t>(n);
    event.condition_m = static_cast<std::uint16_t>(m);
    event.ratchet_count = static_cast<std::uint8_t>(ratchets);
}

struct LuaRandom {
    explicit LuaRandom(std::uint64_t seed) : random(seed) {}
    bool chance(double probability) { return random.chance(probability); }
    int integer(int minimum, int maximum) {
        if (minimum > maximum)
            throw std::runtime_error("random integer minimum exceeds maximum");
        const auto range = static_cast<std::uint64_t>(
                               static_cast<std::int64_t>(maximum) - minimum) +
                           1;
        return minimum + static_cast<int>(random.next_u64() % range);
    }
    int choose(sol::table values) {
        const auto size = values.size();
        if (size == 0)
            throw std::runtime_error("random choice table must not be empty");
        const auto value = values.get<sol::object>(random.index(size) + 1);
        if (!value.is<int>())
            throw std::runtime_error("random note pool must contain integers");
        const auto note = value.as<int>();
        if (note < 0 || note > 127)
            throw std::runtime_error("random note must be 0..127");
        return note;
    }
    DeterministicRandom random;
};

struct LuaTimelineState {
    TimelineTransaction transaction;
    bool staged{};
};
struct LuaTrack {
    std::shared_ptr<LuaTimelineState> owner;
    TrackId id{};
    Track &track() {
        auto *value = owner->transaction.find_track(id);
        if (!value)
            throw std::runtime_error("track no longer exists");
        return *value;
    }
    void set_name(std::string name) {
        std::string error;
        require(owner->transaction.set_track_name(id, std::move(name), error),
                error);
    }
    std::string name() { return track().name; }
    void set_length(MusicalTick length) {
        std::string error;
        require(owner->transaction.set_track_length(id, length, error), error);
    }
    MusicalTick length() { return track().pattern.length; }
    void set_muted(bool muted) {
        std::string error;
        require(owner->transaction.set_track_muted(id, muted, error), error);
    }
    bool muted() { return track().muted; }
    EventId add_note(sol::table note) {
        const auto tick = field_or(note, "tick", MusicalTick{-1});
        const auto pitch = field_or(note, "pitch", -1);
        const auto velocity = field_or(note, "velocity", 1.0);
        const auto duration = field_or(note, "duration", MusicalTick{});
        auto event_id = field_or(note, "id", EventId{});
        if (event_id == 0)
            event_id = owner->transaction.next_event_id();
        if (pitch < 0 || pitch > 127)
            throw std::runtime_error("pitch must be 0..127");
        std::string error;
        require(owner->transaction.add_note(
                    id, event_id, tick, static_cast<std::uint8_t>(pitch),
                    static_cast<float>(velocity), duration, error),
                error);
        for (auto &event : track().pattern.events)
            if (event.id == event_id) {
                apply_trigger(event, note);
                break;
            }
        return event_id;
    }
    void modify_note(EventId event_id, sol::table note) {
        const auto tick = field_or(note, "tick", MusicalTick{-1});
        const auto pitch = field_or(note, "pitch", -1);
        const auto velocity = field_or(note, "velocity", 1.0);
        const auto duration = field_or(note, "duration", MusicalTick{});
        if (pitch < 0 || pitch > 127)
            throw std::runtime_error("pitch must be 0..127");
        std::string error;
        require(owner->transaction.modify_note(
                    id, event_id, tick, static_cast<std::uint8_t>(pitch),
                    static_cast<float>(velocity), duration, error),
                error);
        for (auto &event : track().pattern.events)
            if (event.id == event_id) {
                apply_trigger(event, note);
                break;
            }
    }
    EventId add_parameter(sol::table parameter) {
        const auto tick = field_or(parameter, "tick", MusicalTick{-1});
        const auto parameter_id =
            field_or(parameter, "parameter_id", std::uint32_t{});
        const auto destination =
            field_or(parameter, "destination", std::string{});
        const auto value = field_or(parameter, "value", 0.0);
        auto event_id = field_or(parameter, "id", EventId{});
        if (event_id == 0)
            event_id = owner->transaction.next_event_id();
        std::string error;
        const auto ok = destination.empty()
                            ? owner->transaction.add_parameter(
                                  id, event_id, tick, parameter_id,
                                  static_cast<float>(value), error)
                            : owner->transaction.add_parameter_destination(
                                  id, event_id, tick, destination,
                                  static_cast<float>(value), error);
        require(ok, error);
        return event_id;
    }
    void modify_parameter(EventId event_id, sol::table parameter) {
        const auto tick = field_or(parameter, "tick", MusicalTick{-1});
        const auto parameter_id =
            field_or(parameter, "parameter_id", std::uint32_t{});
        const auto destination =
            field_or(parameter, "destination", std::string{});
        const auto value = field_or(parameter, "value", 0.0);
        std::string error;
        const auto ok = destination.empty()
                            ? owner->transaction.modify_parameter(
                                  id, event_id, tick, parameter_id,
                                  static_cast<float>(value), error)
                            : owner->transaction.modify_parameter_destination(
                                  id, event_id, tick, destination,
                                  static_cast<float>(value), error);
        require(ok, error);
    }
    void remove_event(EventId event_id) {
        std::string error;
        require(owner->transaction.remove_event(id, event_id, error), error);
    }
    sol::table events(sol::this_state state) {
        sol::state_view lua(state);
        auto result = lua.create_table();
        std::size_t index = 1;
        for (const auto &event : track().pattern.events) {
            auto value = lua.create_table();
            value["id"] = event.id;
            value["tick"] = event.tick;
            value["type"] =
                event.type == MusicalEventType::note       ? "note"
                : event.type == MusicalEventType::note_on  ? "note_on"
                : event.type == MusicalEventType::note_off ? "note_off"
                                                           : "parameter";
            value["pitch"] = event.pitch;
            value["velocity"] = event.velocity;
            value["duration"] = event.duration;
            value["parameter_id"] = event.parameter_id;
            value["destination"] = event.parameter_destination;
            value["value"] = event.value;
            value["probability"] = event.trigger_probability;
            value["condition"] = static_cast<int>(event.trigger_condition);
            value["condition_n"] = event.condition_n;
            value["condition_m"] = event.condition_m;
            value["ratchets"] = event.ratchet_count;
            result[index++] = value;
        }
        return result;
    }
    void transpose_by(int semitones) {
        std::string error;
        require(transpose(track().pattern, semitones, error), error);
    }
    void shift_by(MusicalTick ticks) {
        std::string error;
        require(shift(track().pattern, ticks, error), error);
    }
    void scale_velocity(double factor) {
        std::string error;
        require(scale_velocities(track().pattern, factor, error), error);
    }
    void quantize_to(MusicalTick grid) {
        std::string error;
        require(quantize(track().pattern, grid, error), error);
    }
    void humanize_by(LuaRandom &random, MusicalTick timing, double velocity) {
        std::string error;
        require(
            humanize(track().pattern, random.random, timing, velocity, error),
            error);
    }
    void reverse_order() {
        std::string error;
        require(reverse(track().pattern, error), error);
    }
    void rotate_by(MusicalTick ticks) {
        std::string error;
        require(rotate(track().pattern, ticks, error), error);
    }
    void duplicate_at(sol::table offsets) {
        std::vector<MusicalTick> values;
        values.reserve(offsets.size());
        for (std::size_t i = 1; i <= offsets.size(); ++i)
            values.push_back(offsets.get<MusicalTick>(i));
        auto next = owner->transaction.next_event_id();
        std::string error;
        require(duplicate(track().pattern, values, next, error), error);
    }
    void euclidean(std::size_t steps, std::size_t pulses,
                   std::size_t rotation_value, int pitch, double velocity,
                   MusicalTick duration) {
        if (pitch < 0 || pitch > 127)
            throw std::runtime_error("pitch must be 0..127");
        std::string error;
        require(generate_euclidean_notes(
                    owner->transaction, id, steps, pulses, rotation_value,
                    static_cast<std::uint8_t>(pitch),
                    static_cast<float>(velocity), duration, error),
                error);
    }
};

struct LuaTimeline {
    std::shared_ptr<LuaTimelineState> state;
    TimelineService *service{};
    std::function<void(TimelineTransaction)> stage;
    LuaTrack track(TrackId id) {
        if (!state->transaction.find_track(id))
            throw std::runtime_error("track does not exist");
        return {state, id};
    }
    LuaTrack create_track(sol::optional<TrackId> requested, std::string name,
                          MusicalTick length) {
        const auto id = requested.value_or(state->transaction.next_track_id());
        std::string error;
        require(
            state->transaction.create_track(id, std::move(name), length, error),
            error);
        return {state, id};
    }
    void delete_track(TrackId id) {
        std::string error;
        require(state->transaction.delete_track(id, error), error);
    }
    sol::table tracks(sol::this_state lua_state) {
        sol::state_view lua(lua_state);
        auto result = lua.create_table();
        std::size_t index = 1;
        for (const auto &value : state->transaction.timeline().tracks)
            result[index++] = LuaTrack{state, value.id};
        return result;
    }
    std::uint64_t revision() { return state->transaction.base_revision(); }
    std::uint64_t seed() { return state->transaction.generation_seed(); }
    void set_seed(std::uint64_t seed_value) {
        state->transaction.set_generation_seed(seed_value);
    }
    double tempo() {
        return 60'000'000.0 /
               state->transaction.timeline().transport.microseconds_per_quarter;
    }
    std::tuple<std::uint16_t, std::uint16_t> time_signature() {
        const auto &transport = state->transaction.timeline().transport;
        return {transport.time_signature_numerator,
                transport.time_signature_denominator};
    }
    void set_tempo(double bpm) {
        std::string error;
        require(state->transaction.set_tempo_bpm(bpm, error), error);
    }
    void set_time_signature(std::uint16_t numerator,
                            std::uint16_t denominator) {
        std::string error;
        require(state->transaction.set_time_signature(numerator, denominator,
                                                      error),
                error);
    }
    void commit() {
        if (state->staged)
            throw std::runtime_error("working copy was already committed");
        std::string error;
        require(service->validate(state->transaction, error), error);
        stage(state->transaction);
        state->staged = true;
    }
};
} // namespace
#endif

struct ScriptRuntime::Impl {
    Submit submit;
    Position position;
    TimelineService *timeline{};
    std::optional<TimelineTransaction> pending;
#ifdef NEUROGENIC_HAS_LUA
    sol::state lua;
#endif
    Impl(Submit s, Position p, TimelineService *t)
        : submit(std::move(s)), position(std::move(p)), timeline(t) {}
    bool finish(std::string &error) {
        if (!pending)
            return true;
        if (!timeline->commit(std::move(*pending),
                              TimelineCommitPolicy::next_audio_callback,
                              error)) {
            pending.reset();
            return false;
        }
        pending.reset();
        return true;
    }
};

ScriptRuntime::ScriptRuntime(Submit submit, Position position,
                             TimelineService *timeline)
    : impl_(std::make_unique<Impl>(std::move(submit), std::move(position),
                                   timeline)) {
#ifdef NEUROGENIC_HAS_LUA
    struct LuaPattern {
        Pattern value;
        void set_step(int index, bool enabled, int note, double velocity) {
            if (index < 1 || index > 16)
                throw std::runtime_error("step index must be 1..16");
            if (note < 0 || note > 127)
                throw std::runtime_error("note must be 0..127");
            if (velocity < 0 || velocity > 1)
                throw std::runtime_error("velocity must be 0..1");
            auto &s = value.steps[static_cast<std::size_t>(index - 1)];
            s.enabled = enabled;
            s.note = static_cast<std::uint8_t>(note);
            s.velocity = static_cast<float>(velocity);
        }
    };
    impl_->lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                              sol::lib::string);
    impl_->lua.new_usertype<LuaPattern>(std::string("NeurogenicPattern"),
                                        "set_step", &LuaPattern::set_step);
    impl_->lua.new_usertype<LuaRandom>(
        std::string("NeurogenicRandom"), "chance", &LuaRandom::chance,
        "integer", &LuaRandom::integer, "choose", &LuaRandom::choose);
    impl_->lua.new_usertype<LuaTrack>(
        std::string("NeurogenicTrack"), "id", sol::readonly(&LuaTrack::id),
        "name", &LuaTrack::name, "set_name", &LuaTrack::set_name, "length",
        &LuaTrack::length, "set_length", &LuaTrack::set_length, "muted",
        &LuaTrack::muted, "set_muted", &LuaTrack::set_muted, "add_note",
        &LuaTrack::add_note, "modify_note", &LuaTrack::modify_note,
        "add_parameter", &LuaTrack::add_parameter, "modify_parameter",
        &LuaTrack::modify_parameter, "remove_event", &LuaTrack::remove_event,
        "events", &LuaTrack::events, "transpose", &LuaTrack::transpose_by,
        "shift", &LuaTrack::shift_by, "scale_velocities",
        &LuaTrack::scale_velocity, "quantize", &LuaTrack::quantize_to,
        "humanize", &LuaTrack::humanize_by, "reverse", &LuaTrack::reverse_order,
        "rotate", &LuaTrack::rotate_by, "duplicate", &LuaTrack::duplicate_at,
        "generate_euclidean", &LuaTrack::euclidean);
    impl_->lua.new_usertype<LuaTimeline>(
        std::string("NeurogenicTimeline"), "track", &LuaTimeline::track,
        "create_track", &LuaTimeline::create_track, "delete_track",
        &LuaTimeline::delete_track, "tracks", &LuaTimeline::tracks, "revision",
        &LuaTimeline::revision, "seed", &LuaTimeline::seed, "set_seed",
        &LuaTimeline::set_seed, "tempo", &LuaTimeline::tempo, "time_signature",
        &LuaTimeline::time_signature, "set_tempo", &LuaTimeline::set_tempo,
        "set_time_signature", &LuaTimeline::set_time_signature, "commit",
        &LuaTimeline::commit);
    auto ng = impl_->lua.create_named_table("neurogenic");
    ng.set_function("random",
                    [](std::uint64_t seed) { return LuaRandom{seed}; });
    auto v1 = impl_->lua.create_table();
    ng["v1"] = v1;
    auto pattern = impl_->lua.create_table();
    v1["pattern"] = pattern;
    pattern.set_function("new", [] { return LuaPattern{}; });
    pattern.set_function("queue", [this](LuaPattern &p) {
        if (!valid_pattern(p.value) || !impl_->submit(p.value))
            throw std::runtime_error("pattern queue full or invalid");
    });
    auto transport = impl_->lua.create_table();
    v1["transport"] = transport;
    transport.set_function("frame", [this] { return impl_->position(); });
    auto timeline_api = impl_->lua.create_table();
    v1["timeline"] = timeline_api;
    timeline_api.set_function("current", [this] {
        if (!impl_->timeline)
            throw std::runtime_error("timeline editing service is unavailable");
        auto state = std::make_shared<LuaTimelineState>();
        state->transaction = impl_->timeline->edit();
        return LuaTimeline{
            state, impl_->timeline, [this](TimelineTransaction transaction) {
                if (impl_->pending)
                    throw std::runtime_error("only one timeline commit is "
                                             "allowed per script execution");
                impl_->pending = std::move(transaction);
            }};
    });
#endif
}
ScriptRuntime::~ScriptRuntime() = default;

bool ScriptRuntime::execute(const std::string &source,
                            std::string &error) noexcept {
#ifdef NEUROGENIC_HAS_LUA
    try {
        impl_->pending.reset();
        instruction_budget = 10'000'000;
        lua_sethook(impl_->lua.lua_state(), instruction_hook, LUA_MASKCOUNT,
                    1000);
        auto result = impl_->lua.safe_script(source, sol::script_pass_on_error);
        lua_sethook(impl_->lua.lua_state(), nullptr, 0, 0);
        if (!result.valid()) {
            impl_->pending.reset();
            sol::error exception = result;
            error = exception.what();
            return false;
        }
        if (!impl_->finish(error))
            return false;
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        lua_sethook(impl_->lua.lua_state(), nullptr, 0, 0);
        impl_->pending.reset();
        error = exception.what();
        return false;
    }
#else
    (void)source;
    error = "Lua support was not built";
    return false;
#endif
}
bool ScriptRuntime::execute_file(const std::filesystem::path &path,
                                 std::string &error) noexcept {
#ifdef NEUROGENIC_HAS_LUA
    try {
        impl_->pending.reset();
        instruction_budget = 10'000'000;
        lua_sethook(impl_->lua.lua_state(), instruction_hook, LUA_MASKCOUNT,
                    1000);
        auto result = impl_->lua.safe_script_file(path.string(),
                                                  sol::script_pass_on_error);
        lua_sethook(impl_->lua.lua_state(), nullptr, 0, 0);
        if (!result.valid()) {
            impl_->pending.reset();
            sol::error exception = result;
            error = exception.what();
            return false;
        }
        if (!impl_->finish(error))
            return false;
        error.clear();
        return true;
    } catch (const std::exception &exception) {
        lua_sethook(impl_->lua.lua_state(), nullptr, 0, 0);
        impl_->pending.reset();
        error = exception.what();
        return false;
    }
#else
    (void)path;
    error = "Lua support was not built";
    return false;
#endif
}
} // namespace neurogenic
