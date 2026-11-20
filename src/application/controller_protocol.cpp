#include "neurogenic/application/controller_protocol.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <type_traits>
#include <unistd.h>

namespace neurogenic {
namespace {
constexpr std::uint16_t magic = 0x474e;
std::uint16_t read16(const std::uint8_t *p) {
    return std::uint16_t(p[0]) | std::uint16_t(p[1]) << 8;
}
std::uint32_t read32(const std::uint8_t *p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
void append16(std::vector<std::uint8_t> &v, std::uint16_t n) {
    v.push_back(n & 255);
    v.push_back(n >> 8);
}
void append32(std::vector<std::uint8_t> &v, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        v.push_back((n >> (i * 8)) & 255);
}
std::uint16_t crc(std::span<const std::uint8_t> data) {
    std::uint16_t value = 0xffff;
    for (auto byte : data) {
        value ^= std::uint16_t(byte) << 8;
        for (int i = 0; i < 8; ++i)
            value = value & 0x8000 ? std::uint16_t((value << 1) ^ 0x1021)
                                   : std::uint16_t(value << 1);
    }
    return value;
}
std::string escape(std::string_view s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\')
            r += '\\';
        if (static_cast<unsigned char>(c) >= 32)
            r += c;
    }
    return r;
}
std::string mode_name(std::uint16_t id) {
    switch (id) {
    case 0:
        return "step";
    case 1:
        return "pad";
    case 2:
        return "levels";
    case 3:
        return "notes";
    default:
        return {};
    }
}
} // namespace
std::vector<std::uint8_t>
encode_controller_packet(const ControllerPacket &packet) {
    if (packet.payload.size() > controller_maximum_payload)
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(packet.payload.size() + 12);
    append16(out, magic);
    out.push_back(controller_protocol_major);
    out.push_back(static_cast<std::uint8_t>(packet.type));
    append16(out, static_cast<std::uint16_t>(packet.payload.size()));
    append32(out, packet.sequence);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    append16(out, crc(out));
    return out;
}
void ControllerFrameDecoder::push(std::span<const std::uint8_t> data) noexcept {
    for (auto byte : data) {
        if (bytes_.size() == capacity_) {
            bytes_.pop_front();
            ++dropped_;
        }
        bytes_.push_back(byte);
    }
}
std::optional<ControllerPacket> ControllerFrameDecoder::pop() noexcept {
    while (bytes_.size() >= 12) {
        if (bytes_[0] != (magic & 255) || bytes_[1] != (magic >> 8)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        std::array<std::uint8_t, 10> header{};
        for (std::size_t i = 0; i < header.size(); ++i)
            header[i] = bytes_[i];
        if (header[2] != controller_protocol_major) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto length = read16(header.data() + 4);
        if (length > controller_maximum_payload) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        const auto total = std::size_t(length) + 12;
        if (bytes_.size() < total)
            return std::nullopt;
        std::vector<std::uint8_t> raw(total);
        for (std::size_t i = 0; i < total; ++i)
            raw[i] = bytes_[i];
        if (crc(std::span<const std::uint8_t>(raw.data(), total - 2)) !=
            read16(raw.data() + total - 2)) {
            bytes_.pop_front();
            ++invalid_;
            continue;
        }
        for (std::size_t i = 0; i < total; ++i)
            bytes_.pop_front();
        ControllerPacket packet{static_cast<ControllerMessageType>(raw[3]),
                                read32(raw.data() + 6),
                                {}};
        packet.payload.assign(raw.begin() + 10, raw.end() - 2);
        return packet;
    }
    return std::nullopt;
}
ControllerHost::ControllerHost(ApplicationService &app, std::size_t capacity,
                               std::string expected_device_id)
    : application_(&app), capacity_(std::max<std::size_t>(1, capacity)),
      expected_device_id_(std::move(expected_device_id)) {}
bool ControllerHost::enqueue(ControllerPacket p) noexcept {
    if (output_.size() >= capacity_) {
        ++state_.dropped_output;
        return false;
    }
    output_.push_back(std::move(p));
    return true;
}
void ControllerHost::connected() {
    state_.connection = ControllerConnection::negotiating;
    state_.feedback_revision = ~std::uint64_t{};
    enqueue({ControllerMessageType::hello,
             sequence_++,
             {controller_protocol_major, controller_protocol_minor}});
}
void ControllerHost::disconnected(std::string reason) {
    state_.connection = ControllerConnection::disconnected;
    state_.error = std::move(reason);
    output_.clear();
}
void ControllerHost::dispatch_input(std::string id, double value, bool pressed,
                                    std::uint64_t seq) {
    ControllerInput input{std::move(id), value, 0, pressed, seq};
    const auto profile = full_performance_profile();
    std::string error;
    ControllerMapper mapper(profile, application_->actions(), error);
    const auto resolved =
        mapper.resolve(input, application_->snapshot().modifiers, error);
    if (resolved.action) {
        if (!application_->submit(*resolved.action))
            ++state_.dropped_input;
    } else if (!resolved.parameter_id.empty()) {
        ApplicationCommand c;
        c.action_id = resolved.relative_steps ? "parameter.adjust.v1"
                                              : "parameter.set-normalized.v1";
        c.arguments["id"] = resolved.parameter_id;
        c.arguments[resolved.relative_steps ? "steps" : "value"] =
            resolved.relative_steps
                ? resolved.relative_steps
                : std::get<double>(*resolved.parameter_value);
        c.source_id = "physical-controller";
        c.source_sequence = seq;
        if (!application_->submit(std::move(c)))
            ++state_.dropped_input;
    } else
        state_.error = error;
}
bool ControllerHost::receive(const ControllerPacket &p,
                             std::string &error) noexcept {
    ++state_.received;
    if (p.payload.size() > controller_maximum_payload) {
        error = "payload exceeds protocol limit";
        return false;
    }
    if (p.type == ControllerMessageType::hello_reply) {
        if (p.payload.size() < 4) {
            error = "short hello reply";
            return false;
        }
        state_.protocol_major = p.payload[0];
        state_.protocol_minor = p.payload[1];
        const auto idlen = p.payload[2], fwlen = p.payload[3];
        if (4u + idlen + fwlen > p.payload.size()) {
            error = "invalid identity lengths";
            return false;
        }
        state_.device_id.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4), idlen);
        state_.firmware.assign(
            reinterpret_cast<const char *>(p.payload.data() + 4 + idlen),
            fwlen);
        if (state_.protocol_major != controller_protocol_major) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = "protocol major version mismatch";
            error = state_.error;
            return false;
        }
        if (!expected_device_id_.empty() &&
            state_.device_id != expected_device_id_) {
            state_.connection = ControllerConnection::incompatible;
            state_.error = "controller device identity does not match configuration";
            error = state_.error;
            return false;
        }
        state_.connection = ControllerConnection::connected;
        ++state_.reconnects;
        enqueue({ControllerMessageType::capabilities, sequence_++, {}});
        enqueue({ControllerMessageType::state_sync, sequence_++, {}});
    } else if (p.type == ControllerMessageType::capabilities) {
        if (p.payload.size() != 4) {
            error = "invalid capability payload";
            return false;
        }
        state_.capabilities = read32(p.payload.data());
    } else if (p.type == ControllerMessageType::button) {
        if (p.payload.size() != 3) {
            error = "invalid button payload";
            return false;
        }
        const auto id = read16(p.payload.data());
        const bool pressed = p.payload[2] != 0;
        bool queued = true;
        if (id < 16 && pressed) {
            const auto s = application_->snapshot();
            PerformanceGrid grid;
            grid.set_mode(s.pad_mode);
            grid.set_step_page(s.step_page);
            grid.set_selected_slot(s.selected_sample_slot);
            grid.set_selected_pad(s.selected_pad);
            if (auto c =
                    grid_application_command(grid.press(id, s.modifiers.select),
                                             "physical-controller", p.sequence))
                queued = application_->submit(std::move(*c));
        } else if (id >= 16 && id < 24 && pressed)
            queued = application_->submit({"navigation.confirm.v1",
                                           {},
                                           "physical-controller",
                                           p.sequence});
        else if (auto mode = mode_name(id - 24); !mode.empty() && pressed)
            queued = application_->submit({"pad.mode-select.v1",
                                           {{"mode", mode}},
                                           "physical-controller",
                                           p.sequence});
        if (!queued)
            ++state_.dropped_input;
    } else if (p.type == ControllerMessageType::encoder) {
        if (p.payload.size() != 4) {
            error = "invalid encoder payload";
            return false;
        }
        const auto id = read16(p.payload.data());
        const auto delta =
            static_cast<std::int16_t>(read16(p.payload.data() + 2));
        if (id < state_.encoders.size())
            state_.encoders[id] += delta;
        if (id < 8) {
            const auto parameters = application_->snapshot().parameters;
            if (id < parameters.size() &&
                !application_->submit(
                    {"parameter.adjust.v1",
                     {{"id", parameters[id].id}, {"steps", double(delta)}},
                     "physical-controller",
                     p.sequence}))
                ++state_.dropped_input;
        } else
            dispatch_input("data", delta, true, p.sequence);
    } else if (p.type == ControllerMessageType::analog) {
        if (p.payload.size() != 4) {
            error = "invalid analog payload";
            return false;
        }
        const auto id = read16(p.payload.data()),
                   value = read16(p.payload.data() + 2);
        if (id >= state_.analog.size()) {
            error = "unknown analog control";
            return false;
        }
        state_.analog[id] = value;
        const auto normalized = double(value) / 65535.0;
        if (!pickup_[id] && std::abs(normalized - 0.5) < 0.04)
            pickup_[id] = true;
        if (pickup_[id])
            dispatch_input(id < 4 ? "fader." + std::to_string(id + 1)
                                  : "crossfader",
                           normalized, true, p.sequence);
    } else if (p.type == ControllerMessageType::state_sync)
        state_.feedback_revision = ~std::uint64_t{};
    else if (p.type == ControllerMessageType::heartbeat)
        enqueue({ControllerMessageType::heartbeat, sequence_++, p.payload});
    else if (p.type == ControllerMessageType::reset) {
        state_.connection = ControllerConnection::negotiating;
        connected();
    }
    error.clear();
    return true;
}
void ControllerHost::publish(const ApplicationState &s) {
    if (state_.connection != ControllerConnection::connected ||
        state_.feedback_revision == s.revision)
        return;
    state_.feedback_revision = s.revision;
    const std::uint32_t off = 0x101010, selected = 0xff9d00, play = 0x00ff55;
    for (std::size_t i = 0; i < 16; ++i) {
        auto color = off;
        if (s.selected_step == static_cast<std::int64_t>(s.step_page * 16 + i))
            color = selected;
        if (s.playing && i == static_cast<std::size_t>(
                                  (s.transport_tick /
                                   std::max<MusicalTick>(1, s.step_ticks)) %
                                  16))
            color = play;
        state_.leds[i] = color;
        std::vector<std::uint8_t> b;
        append16(b, i);
        append32(b, color);
        b.push_back(255);
        enqueue({ControllerMessageType::led, sequence_++, std::move(b)});
    }
    for (std::size_t i = 0; i < 8; ++i) {
        ContextControl c;
        c.control_id = i;
        c.active = i < s.parameters.size();
        if (c.active) {
            const auto &p = s.parameters[i];
            c.label = p.name;
            c.unit = p.unit;
            c.value = std::visit(
                [](const auto &v) -> std::string {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, double>)
                        return std::to_string(v);
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                        return std::to_string(v);
                    else if constexpr (std::is_same_v<T, bool>)
                        return v ? "On" : "Off";
                    else if constexpr (std::is_same_v<T, std::string>)
                        return v;
                    else
                        return {};
                },
                p.current_value);
        }
        state_.context[i] = c;
        std::string label = c.label.substr(0, 31);
        std::vector<std::uint8_t> b;
        append16(b, i);
        b.push_back(c.active);
        b.push_back(label.size());
        b.insert(b.end(), label.begin(), label.end());
        enqueue({ControllerMessageType::context, sequence_++, std::move(b)});
    }
    static constexpr const char *lower[]{"SELECT", "EDIT",  "PREV", "NEXT",
                                         "BACK",   "ENTER", "PAGE", "MENU"};
    for (std::size_t i = 0; i < 8; ++i) {
        ContextControl c;
        c.control_id = static_cast<std::uint16_t>(8 + i);
        c.label = lower[i];
        c.active = true;
        state_.context[8 + i] = c;
        std::vector<std::uint8_t> b;
        append16(b, c.control_id);
        b.push_back(1);
        b.push_back(c.label.size());
        b.insert(b.end(), c.label.begin(), c.label.end());
        enqueue({ControllerMessageType::context, sequence_++, std::move(b)});
    }
}
std::optional<ControllerPacket> ControllerHost::next_output() noexcept {
    if (output_.empty())
        return {};
    auto p = std::move(output_.front());
    output_.pop_front();
    ++state_.sent;
    return p;
}
ControllerSerialTransport::ControllerSerialTransport(std::size_t c)
    : capacity_(std::max<std::size_t>(1, c)) {}
ControllerSerialTransport::~ControllerSerialTransport() { close(); }
bool ControllerSerialTransport::open(const std::filesystem::path &p,
                                     std::string &e) {
    close();
    fd_ = ::open(p.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        e = std::strerror(errno);
        return false;
    }
    termios t{};
    if (tcgetattr(fd_, &t) == 0) {
        cfmakeraw(&t);
        cfsetspeed(&t, B115200);
        tcsetattr(fd_, TCSANOW, &t);
    }
    identity_ = p;
    e.clear();
    return true;
}
void ControllerSerialTransport::close() noexcept {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    output_.clear();
}
bool ControllerSerialTransport::send(const ControllerPacket &p) noexcept {
    auto bytes = encode_controller_packet(p);
    if (bytes.empty())
        return false;
    if (output_.size() >= capacity_) {
        ++dropped_;
        return false;
    }
    output_.push_back(std::move(bytes));
    return true;
}
bool ControllerSerialTransport::poll(ControllerHost &host,
                                     std::string &e) noexcept {
    if (fd_ < 0) {
        e = "controller serial device is not open";
        return false;
    }
    std::array<std::uint8_t, 512> b{};
    const auto n = ::read(fd_, b.data(), b.size());
    if (n > 0)
        decoder_.push(std::span<const std::uint8_t>(b.data(), n));
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        e = std::strerror(errno);
        host.disconnected(e);
        close();
        return false;
    }
    while (auto p = decoder_.pop())
        host.receive(*p, e);
    while (auto p = host.next_output())
        send(*p);
    while (!output_.empty()) {
        auto &w = output_.front();
        const auto wrote = ::write(fd_, w.data(), w.size());
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (wrote < 0) {
            e = std::strerror(errno);
            host.disconnected(e);
            close();
            return false;
        }
        if (static_cast<std::size_t>(wrote) < w.size()) {
            w.erase(w.begin(), w.begin() + wrote);
            break;
        }
        output_.pop_front();
    }
    e.clear();
    return true;
}
bool SimulatedController::send(ControllerMessageType t,
                               std::vector<std::uint8_t> p) {
    std::string e;
    return host_->receive({t, sequence_++, std::move(p)}, e);
}
bool SimulatedController::negotiate(std::string id) {
    host_->connected();
    std::vector<std::uint8_t> p{controller_protocol_major,
                                controller_protocol_minor,
                                static_cast<std::uint8_t>(id.size()), 3};
    p.insert(p.end(), id.begin(), id.end());
    p.insert(p.end(), {'s', 'i', 'm'});
    return send(ControllerMessageType::hello_reply, std::move(p));
}
bool SimulatedController::button(std::uint16_t id, bool pressed) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    p.push_back(pressed);
    return send(ControllerMessageType::button, std::move(p));
}
bool SimulatedController::encoder(std::uint16_t id, std::int16_t d) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, d);
    return send(ControllerMessageType::encoder, std::move(p));
}
bool SimulatedController::analog(std::uint16_t id, std::uint16_t v) {
    std::vector<std::uint8_t> p;
    append16(p, id);
    append16(p, v);
    return send(ControllerMessageType::analog, std::move(p));
}
std::vector<ControllerPacket> SimulatedController::drain_feedback() {
    std::vector<ControllerPacket> r;
    while (auto p = host_->next_output())
        r.push_back(std::move(*p));
    return r;
}
std::string controller_protocol_json(const ControllerProtocolState &s) {
    return "{\"schema\":\"neurogenic.controller-protocol.v1\",\"connection\":" +
           std::to_string(static_cast<int>(s.connection)) +
           ",\"device_id\":\"" + escape(s.device_id) + "\",\"protocol\":\"" +
           std::to_string(s.protocol_major) + "." +
           std::to_string(s.protocol_minor) +
           "\",\"received\":" + std::to_string(s.received) +
           ",\"sent\":" + std::to_string(s.sent) +
           ",\"dropped_input\":" + std::to_string(s.dropped_input) +
           ",\"dropped_output\":" + std::to_string(s.dropped_output) +
           ",\"reconnects\":" + std::to_string(s.reconnects) +
           ",\"feedback_revision\":" + std::to_string(s.feedback_revision) +
           "}";
}
} // namespace neurogenic
