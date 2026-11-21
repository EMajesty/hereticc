#include "neurogenic/audio/sample_analysis.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
constexpr std::size_t analysis_hop = 128;

void validate(const Sample &sample, SampleAnalysisSettings &settings) {
    if (sample.sample_rate == 0 || sample.frames() == 0 ||
        (sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument("sample is not valid mono/stereo PCM");
    if (settings.end_frame == 0)
        settings.end_frame = sample.frames();
    if (settings.start_frame >= settings.end_frame ||
        settings.end_frame > sample.frames())
        throw std::invalid_argument("analysis range is outside the sample");
    if (!std::isfinite(settings.sensitivity) || settings.sensitivity < 0 ||
        settings.sensitivity > 1 || settings.minimum_separation_frames == 0)
        throw std::invalid_argument("transient settings are invalid");
    if (!std::isfinite(settings.quiet_threshold_db) ||
        !std::isfinite(settings.quiet_hysteresis_db) ||
        settings.quiet_hysteresis_db < 0 ||
        settings.minimum_quiet_frames == 0)
        throw std::invalid_argument("quiet-region settings are invalid");
    if (settings.divisions == 0 ||
        settings.divisions > settings.end_frame - settings.start_frame)
        throw std::invalid_argument("division count exceeds the analysis range");
    if (!std::isfinite(settings.source_bpm) || settings.source_bpm <= 0 ||
        settings.source_bpm > 1000 ||
        !std::isfinite(settings.beats_per_slice) ||
        settings.beats_per_slice <= 0 || settings.beats_per_slice > 1024)
        throw std::invalid_argument("tempo-grid settings are invalid");
}

float rms_block(const Sample &sample, std::uint64_t begin,
                std::uint64_t end) {
    double sum{};
    for (auto frame = begin; frame < end; ++frame) {
        double mixed{};
        for (std::uint16_t channel = 0; channel < sample.channels; ++channel)
            mixed += std::abs(sample.interleaved[frame * sample.channels +
                                                 channel]);
        mixed /= sample.channels;
        sum += mixed * mixed;
    }
    return begin == end ? 0.0F
                        : static_cast<float>(std::sqrt(sum / (end - begin)));
}

void make_regions(SampleAnalysisResult &result) {
    std::vector<std::uint64_t> boundaries{result.range_start};
    for (const auto &candidate : result.candidates)
        if (candidate.enabled && candidate.frame > result.range_start &&
            candidate.frame < result.range_end)
            boundaries.push_back(candidate.frame);
    boundaries.push_back(result.range_end);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    result.suggested_regions.clear();
    for (std::size_t i = 1; i < boundaries.size(); ++i)
        if (boundaries[i - 1] < boundaries[i])
            result.suggested_regions.push_back(
                {boundaries[i - 1], boundaries[i], 1.0F});
}
} // namespace

const char *sample_analysis_algorithm_name(SampleAnalysisAlgorithm value) noexcept {
    switch (value) {
    case SampleAnalysisAlgorithm::transients: return "transients";
    case SampleAnalysisAlgorithm::quiet_regions: return "quiet-regions";
    case SampleAnalysisAlgorithm::equal_divisions: return "equal-divisions";
    case SampleAnalysisAlgorithm::tempo_grid: return "tempo-grid";
    }
    return "unknown";
}
const char *sample_analysis_status_name(SampleAnalysisStatus value) noexcept {
    switch (value) {
    case SampleAnalysisStatus::queued: return "queued";
    case SampleAnalysisStatus::running: return "running";
    case SampleAnalysisStatus::complete: return "complete";
    case SampleAnalysisStatus::cancelled: return "cancelled";
    case SampleAnalysisStatus::failed: return "failed";
    case SampleAnalysisStatus::stale: return "stale";
    }
    return "unknown";
}

SampleAnalysisResult analyze_sample(
    const Sample &sample, SampleSlotId slot, std::uint64_t revision,
    std::uint64_t publication, const SampleAnalysisSettings &input,
    std::stop_token stop, const std::function<void(float)> &progress) {
    auto settings = input;
    validate(sample, settings);
    SampleAnalysisResult result;
    result.source_slot = slot;
    result.source_revision = revision;
    result.source_publication = publication;
    result.sample_rate = sample.sample_rate;
    result.channels = sample.channels;
    result.source_frames = sample.frames();
    result.range_start = settings.start_frame;
    result.range_end = settings.end_frame;
    result.settings = settings;
    const auto emit = [&](std::uint64_t frame, float strength) {
        if (frame > result.range_start && frame < result.range_end)
            result.candidates.push_back({0, frame, std::clamp(strength, 0.0F,
                                                              1.0F), true});
    };

    if (settings.algorithm == SampleAnalysisAlgorithm::equal_divisions) {
        const auto length = result.range_end - result.range_start;
        for (std::uint32_t i = 1; i < settings.divisions; ++i)
            emit(result.range_start + length * i / settings.divisions, 1.0F);
    } else if (settings.algorithm == SampleAnalysisAlgorithm::tempo_grid) {
        const auto spacing = static_cast<std::uint64_t>(std::llround(
            sample.sample_rate * 60.0 / settings.source_bpm *
            settings.beats_per_slice));
        if (spacing == 0 ||
            spacing > static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()))
            throw std::invalid_argument("tempo grid produced zero-frame slices");
        const auto signed_spacing = static_cast<std::int64_t>(spacing);
        auto offset = settings.grid_offset_frames % signed_spacing;
        if (offset < 0) offset += signed_spacing;
        auto delta = static_cast<std::uint64_t>(offset);
        if (delta == 0) delta = spacing;
        for (auto frame = result.range_start + delta;
             frame < result.range_end && frame >= result.range_start;
             frame += spacing)
            emit(frame, 1.0F);
    } else {
        const auto block_count =
            (result.range_end - result.range_start + analysis_hop - 1) /
            analysis_hop;
        std::vector<float> energy(block_count);
        result.peak_temporary_bytes = energy.capacity() * sizeof(float);
        for (std::size_t block = 0; block < block_count; ++block) {
            if (stop.stop_requested())
                throw std::runtime_error("analysis cancelled");
            const auto begin = result.range_start + block * analysis_hop;
            energy[block] = rms_block(
                sample, begin,
                std::min(result.range_end, begin + analysis_hop));
            if (progress && (block & 31U) == 0)
                progress(0.5F * static_cast<float>(block + 1) /
                         static_cast<float>(block_count));
        }
        if (settings.algorithm == SampleAnalysisAlgorithm::transients) {
            float average{}, maximum{};
            std::vector<float> novelty(block_count);
            result.peak_temporary_bytes += novelty.capacity() * sizeof(float);
            for (std::size_t i = 1; i < block_count; ++i) {
                novelty[i] = std::max(0.0F, energy[i] - energy[i - 1]);
                average += novelty[i];
                maximum = std::max(maximum, novelty[i]);
            }
            average /= std::max<std::size_t>(1, block_count - 1);
            const auto threshold =
                average + (0.15F + 1.85F * settings.sensitivity) * average;
            std::uint64_t last = result.range_start;
            for (std::size_t i = 1; i + 1 < block_count; ++i) {
                const auto frame = result.range_start + i * analysis_hop;
                if (novelty[i] >= threshold && novelty[i] >= novelty[i - 1] &&
                    novelty[i] >= novelty[i + 1] &&
                    frame - last >= settings.minimum_separation_frames) {
                    emit(frame, maximum > 0 ? novelty[i] / maximum : 0);
                    last = frame;
                }
            }
            result.diagnostics = "energy-flux onset detector, 128-frame hop";
        } else {
            const auto low = std::pow(10.0F, settings.quiet_threshold_db / 20.0F);
            const auto high = low * std::pow(10.0F,
                                             settings.quiet_hysteresis_db / 20.0F);
            bool quiet = energy.front() <= low;
            std::uint64_t quiet_start = result.range_start;
            for (std::size_t i = 1; i <= block_count; ++i) {
                const auto frame = std::min(result.range_end,
                    result.range_start + i * analysis_hop);
                const auto level = i < block_count ? energy[i] : high + 1;
                if (!quiet && level <= low) {
                    quiet = true;
                    quiet_start = frame;
                } else if (quiet && level >= high) {
                    if (frame - quiet_start >= settings.minimum_quiet_frames) {
                        const auto midpoint = quiet_start + (frame - quiet_start) / 2;
                        const auto padded = std::min(result.range_end,
                            midpoint + settings.boundary_padding_frames);
                        emit(padded, std::clamp((low - *std::min_element(
                            energy.begin(), energy.end())) / std::max(low, 1e-9F),
                            0.0F, 1.0F));
                    }
                    quiet = false;
                }
            }
            result.diagnostics = "RMS quiet detector with hysteresis";
        }
    }
    make_regions(result);
    if (progress) progress(1.0F);
    return result;
}

SampleAnalysisService::SampleAnalysisService(SampleManager &samples,
                                             SliceService &slices)
    : samples_(samples), slices_(slices) {}
SampleAnalysisService::~SampleAnalysisService() {
    std::lock_guard lock(mutex_);
    for (auto &[id, session] : sessions_) {
        (void)id;
        session->worker.request_stop();
    }
}

bool SampleAnalysisService::start(SampleSlotId slot,
                                  SampleAnalysisSettings settings,
                                  SampleAnalysisSessionId &created,
                                  std::string &error) {
    const auto status = samples_.status(slot);
    auto sample = samples_.active_sample(slot);
    if (!sample || status.active_publication == 0) {
        error = "sample slot has no active immutable version";
        return false;
    }
    try {
        auto validation = settings;
        validate(*sample, validation);
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }
    std::lock_guard lock(mutex_);
    if (sessions_.size() >= maximum_sessions) {
        error = "sample analysis session capacity is exhausted";
        return false;
    }
    auto session = std::make_unique<Session>();
    session->state.id = created = next_session_id_++;
    session->state.status = SampleAnalysisStatus::queued;
    session->state.result.source_slot = slot;
    session->state.result.source_revision = status.active_revision;
    session->state.result.source_publication = status.active_publication;
    session->state.result.settings = settings;
    session->source = std::move(sample);
    auto *raw = session.get();
    session->worker = std::jthread([this, raw](std::stop_token stop) {
        {
            std::lock_guard guard(mutex_);
            raw->state.status = SampleAnalysisStatus::running;
        }
        try {
            auto result = analyze_sample(
                *raw->source, raw->state.result.source_slot,
                raw->state.result.source_revision,
                raw->state.result.source_publication,
                raw->state.result.settings, stop,
                [this, raw](float value) {
                    std::lock_guard guard(mutex_);
                    raw->state.progress = value;
                });
            std::lock_guard guard(mutex_);
            if (stop.stop_requested()) {
                raw->state.status = SampleAnalysisStatus::cancelled;
                raw->state.error = "analysis cancelled";
            } else {
                for (auto &candidate : result.candidates)
                    candidate.id = next_candidate_id_++;
                raw->state.result = std::move(result);
                raw->state.progress = 1;
                raw->state.status = SampleAnalysisStatus::complete;
                refresh_stale_locked(*raw);
            }
        } catch (const std::exception &exception) {
            std::lock_guard guard(mutex_);
            raw->state.status = stop.stop_requested()
                                    ? SampleAnalysisStatus::cancelled
                                    : SampleAnalysisStatus::failed;
            raw->state.error = exception.what();
        }
    });
    sessions_.emplace(created, std::move(session));
    error.clear();
    return true;
}

void SampleAnalysisService::refresh_stale_locked(Session &session) const {
    const auto status = samples_.status(session.state.result.source_slot);
    session.state.source_stale =
        status.active_revision != session.state.result.source_revision ||
        status.active_publication != session.state.result.source_publication;
    if (session.state.source_stale &&
        session.state.status == SampleAnalysisStatus::complete)
        session.state.status = SampleAnalysisStatus::stale;
}
bool SampleAnalysisService::editable_locked(Session &session,
                                            std::string &error) const {
    refresh_stale_locked(session);
    if (session.state.status != SampleAnalysisStatus::complete) {
        error = session.state.status == SampleAnalysisStatus::stale
                    ? "analysis source version is stale"
                    : "analysis is not complete";
        return false;
    }
    return true;
}
bool SampleAnalysisService::cancel(SampleAnalysisSessionId id,
                                   std::string &error) {
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(id);
    if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
    found->second->worker.request_stop();
    error.clear(); return true;
}
bool SampleAnalysisService::discard(SampleAnalysisSessionId id,
                                    std::string &error) {
    std::unique_ptr<Session> removed;
    {
        std::lock_guard lock(mutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
        found->second->worker.request_stop();
        removed = std::move(found->second);
        sessions_.erase(found);
    }
    error.clear(); return true;
}
std::optional<SampleAnalysisSnapshot>
SampleAnalysisService::snapshot(SampleAnalysisSessionId id) const {
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(id);
    if (found == sessions_.end()) return std::nullopt;
    refresh_stale_locked(*found->second);
    return found->second->state;
}
std::vector<SampleAnalysisSnapshot> SampleAnalysisService::snapshots() const {
    std::lock_guard lock(mutex_);
    std::vector<SampleAnalysisSnapshot> result;
    result.reserve(sessions_.size());
    for (const auto &[id, session] : sessions_) {
        (void)id; refresh_stale_locked(*session); result.push_back(session->state);
    }
    return result;
}
bool SampleAnalysisService::add_candidate(SampleAnalysisSessionId id,
    std::uint64_t frame, SampleAnalysisCandidateId &created, std::string &error) {
    std::lock_guard lock(mutex_); auto found = sessions_.find(id);
    if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
    auto &session = *found->second;
    if (!editable_locked(session, error) || frame <= session.state.result.range_start ||
        frame >= session.state.result.range_end) { if (error.empty()) error = "candidate is outside analysis range"; return false; }
    session.state.result.candidates.push_back({created = next_candidate_id_++, frame, 1, true});
    make_regions(session.state.result); error.clear(); return true;
}
bool SampleAnalysisService::move_candidate(SampleAnalysisSessionId id,
    SampleAnalysisCandidateId candidate, std::uint64_t frame, std::string &error) {
    std::lock_guard lock(mutex_); auto found = sessions_.find(id);
    if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
    auto &session = *found->second;
    if (!editable_locked(session, error) || frame <= session.state.result.range_start || frame >= session.state.result.range_end) { if (error.empty()) error = "candidate is outside analysis range"; return false; }
    auto item = std::find_if(session.state.result.candidates.begin(), session.state.result.candidates.end(), [&](const auto &v){return v.id == candidate;});
    if (item == session.state.result.candidates.end()) { error = "candidate does not exist"; return false; }
    item->frame = frame; make_regions(session.state.result); error.clear(); return true;
}
bool SampleAnalysisService::remove_candidate(SampleAnalysisSessionId id,
    SampleAnalysisCandidateId candidate, std::string &error) {
    std::lock_guard lock(mutex_); auto found = sessions_.find(id);
    if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
    auto &session = *found->second; if (!editable_locked(session, error)) return false;
    const auto before = session.state.result.candidates.size();
    std::erase_if(session.state.result.candidates, [&](const auto &v){return v.id == candidate;});
    if (before == session.state.result.candidates.size()) { error = "candidate does not exist"; return false; }
    make_regions(session.state.result); error.clear(); return true;
}
bool SampleAnalysisService::set_candidate_enabled(SampleAnalysisSessionId id,
    SampleAnalysisCandidateId candidate, bool enabled, std::string &error) {
    std::lock_guard lock(mutex_); auto found = sessions_.find(id);
    if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
    auto &session = *found->second; if (!editable_locked(session, error)) return false;
    auto item = std::find_if(session.state.result.candidates.begin(), session.state.result.candidates.end(), [&](const auto &v){return v.id == candidate;});
    if (item == session.state.result.candidates.end()) { error = "candidate does not exist"; return false; }
    item->enabled = enabled; make_regions(session.state.result); error.clear(); return true;
}
bool SampleAnalysisService::apply(SampleAnalysisSessionId id,
    SampleAnalysisApplyOptions options, std::string &error) {
    SampleAnalysisResult result;
    {
        std::lock_guard lock(mutex_); auto found = sessions_.find(id);
        if (found == sessions_.end()) { error = "analysis session does not exist"; return false; }
        if (!editable_locked(*found->second, error)) return false;
        result = found->second->state.result;
    }
    std::vector<std::uint64_t> boundaries{result.range_start};
    for (const auto &candidate : result.candidates)
        if (candidate.enabled) boundaries.push_back(candidate.frame);
    boundaries.push_back(result.range_end);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    const auto slice_state = slices_.snapshot(result.source_slot);
    if (slice_state.sample_revision != result.source_revision ||
        slice_state.sample_publication != result.source_publication) {
        error = "analysis source version is stale"; return false;
    }
    return slices_.apply_boundaries(result.source_slot, slice_state.revision,
        boundaries, options.target == SampleAnalysisApplyTarget::regions,
        options.mode == SampleAnalysisApplyMode::replace, options.assign_pads,
        options.starting_pad, error);
}
} // namespace neurogenic
