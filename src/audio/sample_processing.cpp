#include "neurogenic/audio/sample_processing.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace neurogenic {
namespace {
void check_sample(const Sample &sample) {
    if (sample.sample_rate == 0 || sample.frames() == 0 ||
        (sample.channels != 1 && sample.channels != 2) ||
        sample.interleaved.size() % sample.channels != 0)
        throw std::invalid_argument("offline sample must be non-empty mono/stereo PCM");
    if (std::any_of(sample.interleaved.begin(), sample.interleaved.end(),
                    [](float value) { return !std::isfinite(value); }))
        throw std::invalid_argument("offline sample contains NaN or infinity");
}
float db_gain(double db) {
    if (!std::isfinite(db) || db < -120.0 || db > 48.0)
        throw std::invalid_argument("gain must be -120..48 dB");
    return static_cast<float>(std::pow(10.0, db / 20.0));
}
void metrics(const Sample &sample, float &peak, float &rms) {
    double square{}; peak = 0;
    for (const auto value : sample.interleaved) {
        peak = std::max(peak, std::abs(value));
        square += static_cast<double>(value) * value;
    }
    rms = sample.interleaved.empty()
              ? 0
              : static_cast<float>(std::sqrt(square / sample.interleaved.size()));
}
Sample render_range(const Sample &source, std::uint64_t begin,
                    std::uint64_t end, bool reverse) {
    if (begin >= end || end > source.frames())
        throw std::invalid_argument("bounce range is outside the source sample");
    Sample result{source.sample_rate, source.channels, {}};
    result.interleaved.resize((end - begin) * source.channels);
    for (std::uint64_t output = 0; output < end - begin; ++output) {
        const auto input = reverse ? end - 1 - output : begin + output;
        for (std::uint16_t channel = 0; channel < source.channels; ++channel)
            result.interleaved[output * source.channels + channel] =
                source.interleaved[input * source.channels + channel];
    }
    return result;
}
float region_fade(const SampleRegion &region, std::uint64_t relative) {
    float value = 1;
    const auto length = region.end_frame - region.start_frame;
    if (region.fade_in_frames && relative < region.fade_in_frames)
        value = std::min(value, static_cast<float>(relative) /
                                   region.fade_in_frames);
    if (region.fade_out_frames && relative + region.fade_out_frames >= length)
        value = std::min(value, static_cast<float>(length - 1 - relative) /
                                   region.fade_out_frames);
    value = std::clamp(value, 0.0F, 1.0F);
    return region.fade_curve == RegionFadeCurve::equal_power
               ? std::sin(value * 1.57079632679F)
               : value;
}
Sample render_region(const Sample &source, const SampleRegion &region,
                     std::uint64_t requested, bool apply_edit) {
    const auto natural = region.end_frame - region.start_frame;
    const auto frames = requested ? requested : natural;
    if (frames == 0 || region.start_frame >= region.end_frame ||
        region.end_frame > source.frames())
        throw std::invalid_argument("region bounce range is invalid");
    if (requested > natural && !region.loop_enabled)
        throw std::invalid_argument("extended region bounce requires an enabled loop");
    Sample result{source.sample_rate, source.channels, {}};
    result.interleaved.resize(frames * source.channels);
    auto position = region.direction == RegionDirection::reverse
                        ? region.end_frame - 1
                        : region.start_frame;
    for (std::uint64_t output = 0; output < frames; ++output) {
        const auto relative = position - region.start_frame;
        const auto gain = apply_edit ? region.gain * region_fade(region, relative)
                                     : 1.0F;
        for (std::uint16_t channel = 0; channel < source.channels; ++channel)
            result.interleaved[output * source.channels + channel] =
                source.interleaved[position * source.channels + channel] * gain;
        if (region.direction == RegionDirection::forward) {
            ++position;
            if (region.loop_enabled && position >= region.loop_end_frame)
                position = region.loop_start_frame;
            else if (position >= region.end_frame)
                position = region.end_frame - 1;
        } else {
            if (region.loop_enabled && position <= region.loop_start_frame)
                position = region.loop_end_frame - 1;
            else if (position > region.start_frame)
                --position;
        }
    }
    return result;
}
void write_u16(std::ostream &out, std::uint16_t value) {
    const char bytes[]{static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes, 2);
}
void write_u32(std::ostream &out, std::uint32_t value) {
    const char bytes[]{static_cast<char>(value), static_cast<char>(value >> 8),
                       static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes, 4);
}
} // namespace

const char *sample_processing_operation_name(SampleProcessingOperationType v) noexcept {
    switch (v) {
    case SampleProcessingOperationType::trim: return "trim";
    case SampleProcessingOperationType::normalize_peak: return "normalize-peak";
    case SampleProcessingOperationType::normalize_rms: return "normalize-rms";
    case SampleProcessingOperationType::reverse: return "reverse";
    case SampleProcessingOperationType::gain: return "gain";
    case SampleProcessingOperationType::fade: return "fade";
    case SampleProcessingOperationType::insert_silence: return "insert-silence";
    case SampleProcessingOperationType::resample: return "resample";
    case SampleProcessingOperationType::channel_convert: return "channel-convert";
    }
    return "unknown";
}
const char *sample_processing_status_name(SampleProcessingStatus v) noexcept {
    switch (v) {
    case SampleProcessingStatus::draft: return "draft";
    case SampleProcessingStatus::running: return "running";
    case SampleProcessingStatus::complete: return "complete";
    case SampleProcessingStatus::cancelled: return "cancelled";
    case SampleProcessingStatus::failed: return "failed";
    case SampleProcessingStatus::publication_pending: return "publication-pending";
    case SampleProcessingStatus::audio_active: return "audio-active";
    }
    return "unknown";
}

Sample process_sample_offline(Sample sample,
                              std::span<const SampleProcessingOperation> operations,
                              std::stop_token stop,
                              const std::function<void(float)> &progress) {
    check_sample(sample);
    WindowedSincConverter converter;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (stop.stop_requested()) throw std::runtime_error("processing cancelled");
        const auto &operation = operations[index];
        switch (operation.type) {
        case SampleProcessingOperationType::trim:
            sample = render_range(sample, operation.start_frame,
                                  operation.end_frame, false);
            break;
        case SampleProcessingOperationType::normalize_peak:
        case SampleProcessingOperationType::normalize_rms: {
            float peak{}, rms{}; metrics(sample, peak, rms);
            const auto measured = operation.type ==
                                          SampleProcessingOperationType::normalize_peak
                                      ? peak : rms;
            if (measured > 0) {
                const auto gain = db_gain(operation.value) / measured;
                for (auto &value : sample.interleaved) value *= gain;
            }
            break;
        }
        case SampleProcessingOperationType::reverse:
            sample = render_range(sample, 0, sample.frames(), true);
            break;
        case SampleProcessingOperationType::gain: {
            const auto gain = db_gain(operation.value);
            for (auto &value : sample.interleaved) value *= gain;
            break;
        }
        case SampleProcessingOperationType::fade: {
            if (operation.start_frame > sample.frames() ||
                operation.end_frame > sample.frames())
                throw std::invalid_argument("fade exceeds processed sample length");
            for (std::uint64_t frame = 0; frame < sample.frames(); ++frame) {
                float gain = 1;
                if (operation.start_frame && frame < operation.start_frame)
                    gain = std::min(gain, static_cast<float>(frame) /
                                              operation.start_frame);
                if (operation.end_frame && frame + operation.end_frame >= sample.frames())
                    gain = std::min(gain, static_cast<float>(sample.frames() - 1 - frame) /
                                              operation.end_frame);
                for (std::uint16_t channel = 0; channel < sample.channels; ++channel)
                    sample.interleaved[frame * sample.channels + channel] *=
                        std::clamp(gain, 0.0F, 1.0F);
            }
            break;
        }
        case SampleProcessingOperationType::insert_silence: {
            if (operation.frames > std::numeric_limits<std::size_t>::max() /
                                       sample.channels - sample.frames())
                throw std::length_error("silence insertion exceeds addressable memory");
            std::vector<float> silence(operation.frames * sample.channels, 0);
            if (operation.silence_position == SilencePosition::beginning)
                sample.interleaved.insert(sample.interleaved.begin(),
                                          silence.begin(), silence.end());
            else
                sample.interleaved.insert(sample.interleaved.end(),
                                          silence.begin(), silence.end());
            break;
        }
        case SampleProcessingOperationType::resample:
            if (operation.sample_rate < 8000 || operation.sample_rate > 384000)
                throw std::invalid_argument("sample rate must be 8000..384000 Hz");
            sample = converter.convert(sample, operation.sample_rate);
            break;
        case SampleProcessingOperationType::channel_convert:
            if (operation.channels == sample.channels) break;
            if (operation.channels == 1 && sample.channels == 2) {
                std::vector<float> mono(sample.frames());
                for (std::size_t frame = 0; frame < sample.frames(); ++frame)
                    mono[frame] = 0.5F * (sample.interleaved[frame * 2] +
                                          sample.interleaved[frame * 2 + 1]);
                sample.channels = 1; sample.interleaved = std::move(mono);
            } else if (operation.channels == 2 && sample.channels == 1) {
                std::vector<float> stereo(sample.frames() * 2);
                for (std::size_t frame = 0; frame < sample.frames(); ++frame)
                    stereo[frame * 2] = stereo[frame * 2 + 1] =
                        sample.interleaved[frame];
                sample.channels = 2; sample.interleaved = std::move(stereo);
            } else throw std::invalid_argument("channel count must be mono or stereo");
            break;
        }
        check_sample(sample);
        if (progress) progress(static_cast<float>(index + 1) /
                               std::max<std::size_t>(1, operations.size()));
    }
    if (progress && operations.empty()) progress(1);
    return sample;
}

void save_sample_wav_atomic(const Sample &sample,
                            const std::filesystem::path &path,
                            SampleWavFormat format, bool overwrite) {
    check_sample(sample);
    if (path.empty() || path.filename().empty())
        throw std::invalid_argument("output WAV path is empty");
    if (std::filesystem::exists(path) && !overwrite)
        throw std::runtime_error("output asset already exists");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".neurogenic.tmp";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open temporary WAV asset");
        const auto bits = format == SampleWavFormat::pcm16 ? 16U :
                          format == SampleWavFormat::pcm24 ? 24U : 32U;
        const auto bytes = bits / 8;
        const auto payload64 = sample.interleaved.size() * bytes;
        if (payload64 > std::numeric_limits<std::uint32_t>::max() - 36)
            throw std::length_error("WAV asset exceeds RIFF size limit");
        const auto payload = static_cast<std::uint32_t>(payload64);
        out.write("RIFF", 4); write_u32(out, 36 + payload); out.write("WAVEfmt ", 8);
        write_u32(out, 16); write_u16(out, format == SampleWavFormat::float32 ? 3 : 1);
        write_u16(out, sample.channels); write_u32(out, sample.sample_rate);
        write_u32(out, sample.sample_rate * sample.channels * bytes);
        write_u16(out, sample.channels * bytes); write_u16(out, bits);
        out.write("data", 4); write_u32(out, payload);
        for (const auto input : sample.interleaved) {
            const auto value = std::clamp(input, -1.0F, 1.0F);
            if (format == SampleWavFormat::pcm16) {
                const auto encoded = static_cast<std::int16_t>(std::lrint(
                    value * (value < 0 ? 32768.0F : 32767.0F)));
                write_u16(out, static_cast<std::uint16_t>(encoded));
            } else if (format == SampleWavFormat::pcm24) {
                const auto encoded = static_cast<std::int32_t>(std::lrint(
                    value * (value < 0 ? 8388608.0F : 8388607.0F)));
                const char bytes24[]{static_cast<char>(encoded),
                                     static_cast<char>(encoded >> 8),
                                     static_cast<char>(encoded >> 16)};
                out.write(bytes24, 3);
            } else write_u32(out, std::bit_cast<std::uint32_t>(value));
        }
        out.flush();
        if (!out) throw std::runtime_error("failed while writing WAV asset");
        out.close();
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

SampleProcessingService::SampleProcessingService(SampleManager &samples,
                                                 SliceService &slices)
    : samples_(samples), slices_(slices) {}
SampleProcessingService::~SampleProcessingService() {
    std::lock_guard lock(mutex_);
    for (auto &[id, job] : jobs_) { (void)id; job->worker.request_stop(); }
}
bool SampleProcessingService::prepare_source_locked(Job &job, std::string &error) {
    const auto status = samples_.status(job.state.source.slot);
    job.source_sample = samples_.active_sample(job.state.source.slot);
    if (!job.source_sample || status.active_publication == 0) {
        error = "source slot has no active immutable sample"; return false;
    }
    job.state.source_revision = status.active_revision;
    job.state.source_publication = status.active_publication;
    const auto snapshot = slices_.snapshot(job.state.source.slot);
    if (job.state.source.kind == SampleProcessingSourceKind::region) {
        const auto found = std::find_if(snapshot.regions.begin(), snapshot.regions.end(),
            [&](const auto &v){ return v.id == job.state.source.region; });
        if (found == snapshot.regions.end()) { error = "source region does not exist"; return false; }
        job.source_region = *found;
    } else if (job.state.source.kind == SampleProcessingSourceKind::slice) {
        const auto found = std::find_if(snapshot.slices.begin(), snapshot.slices.end(),
            [&](const auto &v){ return v.id == job.state.source.slice; });
        if (found == snapshot.slices.end()) { error = "source slice does not exist"; return false; }
        job.source_slice = *found;
    }
    return true;
}
bool SampleProcessingService::create(SampleProcessingSource source,
    SampleProcessingJobId &created, std::string &error) {
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= maximum_jobs) { error = "processing job capacity is exhausted"; return false; }
    auto job = std::make_unique<Job>(); job->state.id = created = next_job_id_++;
    job->state.source = source;
    if (!prepare_source_locked(*job, error)) return false;
    jobs_.emplace(created, std::move(job)); error.clear(); return true;
}
bool SampleProcessingService::add_operation(SampleProcessingJobId id,
    SampleProcessingOperation op, SampleProcessingOperationId &created,
    std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    auto &state = found->second->state;
    if (state.status != SampleProcessingStatus::draft) { error = "processing job is not editable"; return false; }
    if (state.operations.size() >= maximum_operations) { error = "processing operation capacity is exhausted"; return false; }
    op.id = created = next_operation_id_++; state.operations.push_back(op); error.clear(); return true;
}
bool SampleProcessingService::remove_operation(SampleProcessingJobId id,
    SampleProcessingOperationId op, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    auto &state = found->second->state;
    if (state.status != SampleProcessingStatus::draft) { error = "processing job is not editable"; return false; }
    const auto before = state.operations.size();
    std::erase_if(state.operations, [&](const auto &v){return v.id == op;});
    if (before == state.operations.size()) { error = "processing operation does not exist"; return false; }
    error.clear(); return true;
}
bool SampleProcessingService::move_operation(SampleProcessingJobId id,
    SampleProcessingOperationId op, std::size_t position, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    auto &state = found->second->state;
    if (state.status != SampleProcessingStatus::draft || position >= state.operations.size()) { error = "operation move is invalid"; return false; }
    const auto item = std::find_if(state.operations.begin(), state.operations.end(), [&](const auto &v){return v.id == op;});
    if (item == state.operations.end()) { error = "processing operation does not exist"; return false; }
    auto value = *item; state.operations.erase(item); state.operations.insert(state.operations.begin() + position, value); error.clear(); return true;
}
bool SampleProcessingService::validate(SampleProcessingJobId id, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    try {
        Sample source;
        const auto &job = *found->second;
        if (job.source_region) source = render_region(*job.source_sample, *job.source_region,
            job.state.source.loop_output_frames, job.state.source.apply_region_gain_and_fades);
        else if (job.source_slice) source = render_range(*job.source_sample,
            job.source_slice->start_frame, job.source_slice->end_frame, false);
        else source = *job.source_sample;
        (void)process_sample_offline(std::move(source), job.state.operations);
        error.clear(); return true;
    } catch (const std::exception &e) { error = e.what(); return false; }
}
bool SampleProcessingService::start(SampleProcessingJobId id, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    auto *job = found->second.get();
    if (job->state.status != SampleProcessingStatus::draft) { error = "processing job is not a draft"; return false; }
    job->state.status = SampleProcessingStatus::running;
    job->worker = std::jthread([this, job](std::stop_token stop) {
        const auto begin = std::chrono::steady_clock::now();
        try {
            Sample source;
            if (job->source_region) source = render_region(*job->source_sample,
                *job->source_region, job->state.source.loop_output_frames,
                job->state.source.apply_region_gain_and_fades);
            else if (job->source_slice) source = render_range(*job->source_sample,
                job->source_slice->start_frame, job->source_slice->end_frame, false);
            else source = *job->source_sample;
            auto result = process_sample_offline(std::move(source), job->state.operations,
                stop, [this, job](float progress){ std::lock_guard guard(mutex_); job->state.progress = progress; });
            auto prepared = std::make_shared<const Sample>(std::move(result));
            float peak{}, rms{}; metrics(*prepared, peak, rms);
            std::lock_guard guard(mutex_);
            if (stop.stop_requested()) { job->state.status = SampleProcessingStatus::cancelled; job->state.error = "processing cancelled"; return; }
            job->result_sample = std::move(prepared);
            job->state.result = {job->result_sample->sample_rate,
                job->result_sample->channels, job->result_sample->frames(),
                job->result_sample->interleaved.size() * sizeof(float),
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - begin).count()), peak,
                rms, {}, invalid_sample_slot};
            job->state.progress = 1; job->state.status = SampleProcessingStatus::complete;
        } catch (const std::exception &e) {
            std::lock_guard guard(mutex_); job->state.status = stop.stop_requested() ? SampleProcessingStatus::cancelled : SampleProcessingStatus::failed; job->state.error = e.what();
        }
    });
    error.clear(); return true;
}
bool SampleProcessingService::cancel(SampleProcessingJobId id, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
    auto &job = *found->second;
    if (job.state.status == SampleProcessingStatus::draft) {
        job.state.status = SampleProcessingStatus::cancelled;
        job.state.error = "processing cancelled";
    } else if (job.state.status == SampleProcessingStatus::running) {
        job.worker.request_stop();
    } else {
        error = "processing job is not cancellable";
        return false;
    }
    error.clear(); return true;
}
bool SampleProcessingService::discard(SampleProcessingJobId id, std::string &error) {
    std::unique_ptr<Job> removed;
    { std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
      if (found == jobs_.end()) { error = "processing job does not exist"; return false; }
      found->second->worker.request_stop(); removed = std::move(found->second); jobs_.erase(found); }
    error.clear(); return true;
}
bool SampleProcessingService::save(SampleProcessingJobId id,
    const std::filesystem::path &path, SampleWavFormat format, bool overwrite,
    std::string &error) {
    std::shared_ptr<const Sample> result;
    { std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
      if (found == jobs_.end() || !found->second->result_sample) { error = "processing result is unavailable"; return false; }
      result = found->second->result_sample; }
    try { save_sample_wav_atomic(*result, path, format, overwrite); }
    catch (const std::exception &e) { error = e.what(); return false; }
    std::lock_guard lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) {
        error = "processing job was discarded while saving";
        return false;
    }
    found->second->state.result.saved_path = path;
    error.clear(); return true;
}
bool SampleProcessingService::publish_to_slot(SampleProcessingJobId id,
    SampleSlotId slot, std::string &error) {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end() || !found->second->result_sample) { error = "processing result is unavailable"; return false; }
    auto &job = *found->second;
    if (!samples_.publish_prepared(slot, job.result_sample,
            job.state.result.saved_path.empty() ? std::filesystem::path{"processed-memory.wav"} : job.state.result.saved_path)) {
        error = "sample publication queue rejected processed result"; return false;
    }
    job.state.result.destination = slot;
    job.pending_destination_revision = samples_.status(slot).revision;
    job.state.status = SampleProcessingStatus::publication_pending;
    error.clear(); return true;
}
void SampleProcessingService::refresh_publication_locked(Job &job) const {
    if (job.state.status != SampleProcessingStatus::publication_pending) return;
    const auto status = samples_.status(job.state.result.destination);
    if (status.active_revision == job.pending_destination_revision && status.has_sample)
        job.state.status = SampleProcessingStatus::audio_active;
}
std::optional<SampleProcessingSnapshot> SampleProcessingService::snapshot(SampleProcessingJobId id) const {
    std::lock_guard lock(mutex_); const auto found = jobs_.find(id);
    if (found == jobs_.end()) return std::nullopt;
    refresh_publication_locked(*found->second); return found->second->state;
}
std::vector<SampleProcessingSnapshot> SampleProcessingService::snapshots() const {
    std::lock_guard lock(mutex_); std::vector<SampleProcessingSnapshot> result;
    result.reserve(jobs_.size());
    for (const auto &[id, job] : jobs_) { (void)id; refresh_publication_locked(*job); result.push_back(job->state); }
    return result;
}
} // namespace neurogenic
