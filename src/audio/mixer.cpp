#include "neurogenic/audio/mixer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurogenic {
namespace {
bool finite_range(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
std::pair<float, float> pan(float value) noexcept {
    const auto position = std::clamp(value, -1.0F, 1.0F);
    return {position > 0 ? 1.0F - position : 1.0F,
            position < 0 ? 1.0F + position : 1.0F};
}
} // namespace

bool validate_mixer_configuration(const MixerConfiguration &value,
                                  std::uint8_t inputs,
                                  std::uint8_t outputs,
                                  std::string &error) noexcept {
    if (value.track_count > maximum_mixer_tracks ||
        value.input_count > maximum_mixer_inputs || outputs > maximum_mixer_outputs ||
        inputs > maximum_mixer_inputs) {
        error = "mixer capacity exceeded";
        return false;
    }
    if (outputs && (value.master.output_left >= outputs ||
                    value.master.output_right >= outputs ||
                    value.master.output_left == value.master.output_right)) {
        error = "master output mapping is unavailable";
        return false;
    }
    if (!finite_range(value.master.gain, 0, 2)) {
        error = "master gain must be 0..2";
        return false;
    }
    for (std::size_t i = 0; i < value.track_count; ++i) {
        const auto &channel = value.tracks[i];
        if (!channel.track || !finite_range(channel.gain, 0, 2) ||
            !finite_range(channel.pan, -1, 1)) {
            error = "invalid track mixer channel";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (value.tracks[j].track == channel.track) {
                error = "duplicate track mixer channel";
                return false;
            }
        for (const auto send : channel.aux_send)
            if (!finite_range(send, 0, 1)) {
                error = "aux send must be 0..1";
                return false;
            }
        if (channel.output_pair < -1 ||
            (channel.output_pair >= 0 &&
             (!outputs || channel.output_pair + 1 >= outputs))) {
            error = "track output pair is unavailable";
            return false;
        }
    }
    for (std::size_t i = 0; i < value.input_count; ++i) {
        const auto &channel = value.inputs[i];
        if (channel.physical_channel >= inputs ||
            !finite_range(channel.gain, 0, 2) ||
            !finite_range(channel.pan, -1, 1)) {
            error = "physical input mapping is unavailable";
            return false;
        }
    }
    for (const auto &aux : value.aux)
        if (!finite_range(aux.gain, 0, 2) ||
            !finite_range(aux.return_gain, 0, 2) ||
            aux.output_pair < -1 ||
            (aux.output_pair >= 0 &&
             (!outputs || aux.output_pair + 1 >= outputs))) {
            error = "invalid auxiliary bus configuration";
            return false;
        }
    error.clear();
    return true;
}

Mixer::Mixer(std::uint32_t sample_rate)
    : sample_rate_(sample_rate),
      aux_effects_{StereoDelay(sample_rate), StereoDelay(sample_rate)} {
    for (auto &effect : aux_effects_) {
        effect.set_parameter(static_cast<std::uint32_t>(DelayParameter::mix),
                             0.25F);
        effect.set_parameter(
            static_cast<std::uint32_t>(DelayParameter::feedback), 0.25F);
    }
}
bool Mixer::publish(const MixerConfiguration &value,
                    std::string &error) noexcept {
    if (!validate_mixer_configuration(value, maximum_mixer_inputs,
                                      maximum_mixer_outputs, error))
        return false;
    if (!publications_.push(value)) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        error = "mixer publication queue is full";
        return false;
    }
    {
        std::lock_guard lock(control_mutex_);
        control_ = value;
    }
    error.clear();
    return true;
}
void Mixer::begin_block(std::size_t frames) noexcept {
    MixerConfiguration pending;
    while (publications_.pop(pending))
        active_ = pending;
    revision_.store(active_.revision, std::memory_order_release);
    frames_ = std::min(frames, maximum_mixer_block);
    for (auto &buffer : track_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : track_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : input_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : aux_left_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
    for (auto &buffer : aux_right_)
        std::fill_n(buffer.begin(), frames_, 0.0F);
}
std::size_t Mixer::find_track(TrackId track) const noexcept {
    for (std::size_t i = 0; i < active_.track_count; ++i)
        if (active_.tracks[i].track == track)
            return i;
    return maximum_mixer_tracks;
}
bool Mixer::add_track(TrackId track, std::size_t frame, float left,
                      float right) noexcept {
    if (frame >= frames_)
        return false;
    const auto channel = find_track(track);
    if (channel == maximum_mixer_tracks)
        return false;
    track_left_[channel][frame] += left;
    track_right_[channel][frame] += right;
    return true;
}
void Mixer::add_input(std::uint8_t physical, std::size_t frame,
                      float value) noexcept {
    if (frame >= frames_)
        return;
    for (std::size_t i = 0; i < active_.input_count; ++i)
        if (active_.inputs[i].physical_channel == physical)
            input_[i][frame] += value;
}
void Mixer::update_meter(AtomicMeter &meter, float left, float right) noexcept {
    constexpr float decay = 0.92F;
    const auto l = std::max(std::fabs(left),
                            meter.left.load(std::memory_order_relaxed) * decay);
    const auto r = std::max(std::fabs(right),
                            meter.right.load(std::memory_order_relaxed) * decay);
    meter.left.store(l, std::memory_order_relaxed);
    meter.right.store(r, std::memory_order_relaxed);
    if (l >= 1 || r >= 1)
        meter.clip.store(true, std::memory_order_relaxed);
}
void Mixer::process(std::span<float> master_left,
                    std::span<float> master_right,
                    std::span<float> physical, std::uint8_t channels) noexcept {
    const auto frames = std::min({frames_, master_left.size(), master_right.size()});
    bool any_solo{};
    for (std::size_t i = 0; i < active_.track_count; ++i)
        any_solo |= active_.tracks[i].solo;
    for (std::size_t channel = 0; channel < active_.track_count; ++channel) {
        const auto &settings = active_.tracks[channel];
        const auto [pan_l, pan_r] = pan(settings.pan);
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto raw_l = track_left_[channel][frame];
            const auto raw_r = track_right_[channel][frame];
            const auto pre_l = raw_l * pan_l, pre_r = raw_r * pan_r;
            const auto audible = !settings.mute && (!any_solo || settings.solo);
            const auto post_l = audible ? pre_l * settings.gain : 0.0F;
            const auto post_r = audible ? pre_r * settings.gain : 0.0F;
            track_left_[channel][frame] = post_l;
            track_right_[channel][frame] = post_r;
            for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                const auto send_l = settings.aux_pre_fader[aux] ? pre_l : post_l;
                const auto send_r = settings.aux_pre_fader[aux] ? pre_r : post_r;
                aux_left_[aux][frame] += send_l * settings.aux_send[aux];
                aux_right_[aux][frame] += send_r * settings.aux_send[aux];
            }
            if (settings.output_pair < 0) {
                master_left[frame] += post_l;
                master_right[frame] += post_r;
            } else if (channels && physical.size() >= frames * channels) {
                physical[frame * channels + settings.output_pair] += post_l;
                physical[frame * channels + settings.output_pair + 1] += post_r;
            }
            peak_l = std::max(peak_l, std::fabs(post_l));
            peak_r = std::max(peak_r, std::fabs(post_r));
        }
        update_meter(track_meters_[channel], peak_l, peak_r);
    }
    for (std::size_t channel = 0; channel < active_.input_count; ++channel) {
        const auto &settings = active_.inputs[channel];
        const auto [pan_l, pan_r] = pan(settings.pan);
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto value = settings.mute ? 0.0F : input_[channel][frame] * settings.gain;
            const auto l = value * pan_l, r = value * pan_r;
            if (settings.route_to_master) {
                master_left[frame] += l;
                master_right[frame] += r;
            }
            for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
                aux_left_[aux][frame] += l * settings.aux_send[aux];
                aux_right_[aux][frame] += r * settings.aux_send[aux];
            }
            peak_l = std::max(peak_l, std::fabs(l));
            peak_r = std::max(peak_r, std::fabs(r));
        }
        update_meter(input_meters_[channel], peak_l, peak_r);
    }
    for (std::size_t aux = 0; aux < mixer_aux_count; ++aux) {
        const auto &settings = active_.aux[aux];
        if (settings.effect_enabled)
            aux_effects_[aux].process(
                {aux_left_[aux].data(), frames},
                {aux_right_[aux].data(), frames});
        float peak_l{}, peak_r{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto l = settings.mute ? 0.0F : aux_left_[aux][frame] * settings.gain;
            const auto r = settings.mute ? 0.0F : aux_right_[aux][frame] * settings.gain;
            aux_left_[aux][frame] = l;
            aux_right_[aux][frame] = r;
            if (settings.output_pair < 0) {
                master_left[frame] += l * settings.return_gain;
                master_right[frame] += r * settings.return_gain;
            } else if (channels && physical.size() >= frames * channels) {
                physical[frame * channels + settings.output_pair] += l;
                physical[frame * channels + settings.output_pair + 1] += r;
            }
            peak_l = std::max(peak_l, std::fabs(l));
            peak_r = std::max(peak_r, std::fabs(r));
        }
        update_meter(aux_meters_[aux], peak_l, peak_r);
    }
}
void Mixer::apply_master_gain(std::span<float> left,
                              std::span<float> right) noexcept {
    const auto frames = std::min({frames_, left.size(), right.size()});
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (active_.master.mute)
            left[frame] = right[frame] = 0;
        else {
            left[frame] *= active_.master.gain;
            right[frame] *= active_.master.gain;
        }
    }
}
void Mixer::route_master_output(std::span<const float> left,
                                std::span<const float> right,
                                std::span<float> physical,
                                std::uint8_t channels) noexcept {
    const auto frames = std::min({frames_, left.size(), right.size()});
    float peak_l{}, peak_r{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        peak_l = std::max(peak_l, std::fabs(left[frame]));
        peak_r = std::max(peak_r, std::fabs(right[frame]));
        if (channels == 1 && physical.size() >= frames)
            physical[frame] += (left[frame] + right[frame]) * 0.5F;
        else if (channels && physical.size() >= frames * channels &&
            active_.master.output_left < channels &&
            active_.master.output_right < channels) {
            physical[frame * channels + active_.master.output_left] += left[frame];
            physical[frame * channels + active_.master.output_right] += right[frame];
        }
    }
    update_meter(master_meter_, peak_l, peak_r);
}

MixerState Mixer::state() const noexcept {
    MixerState result;
    result.revision = revision_.load(std::memory_order_acquire);
    result.rejected_publications = rejected_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(control_mutex_);
        result.configuration = control_;
    }
    const auto read = [](const AtomicMeter &meter) {
        return StereoMeter{meter.left.load(std::memory_order_relaxed),
                           meter.right.load(std::memory_order_relaxed),
                           meter.clip.load(std::memory_order_relaxed)};
    };
    for (std::size_t i = 0; i < maximum_mixer_tracks; ++i)
        result.tracks[i] = read(track_meters_[i]);
    for (std::size_t i = 0; i < maximum_mixer_inputs; ++i)
        result.inputs[i] = read(input_meters_[i]);
    for (std::size_t i = 0; i < mixer_aux_count; ++i)
        result.aux[i] = read(aux_meters_[i]);
    result.master = read(master_meter_);
    return result;
}
std::span<const float> Mixer::track_left(std::size_t index) const noexcept {
    return index < maximum_mixer_tracks
               ? std::span<const float>{track_left_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::track_right(std::size_t index) const noexcept {
    return index < maximum_mixer_tracks
               ? std::span<const float>{track_right_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::aux_left(std::size_t index) const noexcept {
    return index < mixer_aux_count
               ? std::span<const float>{aux_left_[index].data(), frames_}
               : std::span<const float>{};
}
std::span<const float> Mixer::aux_right(std::size_t index) const noexcept {
    return index < mixer_aux_count
               ? std::span<const float>{aux_right_[index].data(), frames_}
               : std::span<const float>{};
}
} // namespace neurogenic
