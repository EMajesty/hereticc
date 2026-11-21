#include "neurogenic/audio/sample_processing.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void near(float actual, float expected, float tolerance, const char *message) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}
neurogenic::Sample mono(std::initializer_list<float> values,
                        std::uint32_t rate = 48000) {
    return {rate, 1, values};
}
void wait(neurogenic::SampleProcessingService &service,
          neurogenic::SampleProcessingJobId id) {
    for (int i = 0; i < 2000; ++i) {
        const auto state = service.snapshot(id);
        check(state.has_value(), "processing job disappeared");
        if (state->status == neurogenic::SampleProcessingStatus::complete ||
            state->status == neurogenic::SampleProcessingStatus::failed ||
            state->status == neurogenic::SampleProcessingStatus::cancelled)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("processing job timed out");
}
}

int main() try {
    using namespace neurogenic;
    const SampleProcessingOperation trim{1, SampleProcessingOperationType::trim,
                                         1, 4};
    auto result = process_sample_offline(mono({0, .25F, .5F, 1, 0}), {&trim, 1});
    check(result.frames() == 3 && result.interleaved.front() == .25F &&
              result.interleaved.back() == 1,
          "trim PCM mismatch");
    SampleProcessingOperation reverse{1, SampleProcessingOperationType::reverse};
    result = process_sample_offline(result, {&reverse, 1});
    check(result.interleaved.front() == 1 && result.interleaved.back() == .25F,
          "reverse PCM mismatch");

    SampleProcessingOperation peak{1, SampleProcessingOperationType::normalize_peak};
    peak.value = -6.020599913;
    result = process_sample_offline(mono({-.25F, .5F}), {&peak, 1});
    near(result.interleaved[1], .5F, .0001F, "peak normalization mismatch");
    SampleProcessingOperation rms{1, SampleProcessingOperationType::normalize_rms};
    rms.value = -6.020599913;
    result = process_sample_offline(mono({.25F, -.25F}), {&rms, 1});
    near(std::abs(result.interleaved[0]), .5F, .0001F,
         "RMS normalization mismatch");
    result = process_sample_offline(mono({0, 0}), {&peak, 1});
    check(result.interleaved[0] == 0, "silent normalization created nonzero PCM");

    SampleProcessingOperation gain{1, SampleProcessingOperationType::gain};
    gain.value = 6.020599913;
    SampleProcessingOperation fade{2, SampleProcessingOperationType::fade, 2, 2};
    std::array chain{gain, fade};
    result = process_sample_offline(mono({.25F, .25F, .25F, .25F, .25F}), chain);
    check(result.interleaved.front() == 0 && result.interleaved.back() == 0 &&
              result.interleaved[2] > .49F,
          "gain/fade ordering mismatch");
    SampleProcessingOperation silence{1, SampleProcessingOperationType::insert_silence};
    silence.frames = 2; silence.silence_position = SilencePosition::beginning;
    result = process_sample_offline(mono({.5F}), {&silence, 1});
    check(result.frames() == 3 && result.interleaved[0] == 0 &&
              result.interleaved[2] == .5F,
          "silence insertion mismatch");

    Sample stereo{48000, 2, {1, -1, .5F, .5F}};
    SampleProcessingOperation channels{1, SampleProcessingOperationType::channel_convert};
    channels.channels = 1;
    result = process_sample_offline(stereo, {&channels, 1});
    check(result.channels == 1 && result.frames() == 2 &&
              result.interleaved[0] == 0 && result.interleaved[1] == .5F,
          "stereo to mono conversion mismatch");
    channels.channels = 2;
    result = process_sample_offline(result, {&channels, 1});
    check(result.channels == 2 && result.interleaved[2] == result.interleaved[3],
          "mono to stereo conversion mismatch");
    SampleProcessingOperation resample{1, SampleProcessingOperationType::resample};
    resample.sample_rate = 24000;
    result = process_sample_offline(mono({0, .25F, .5F, .75F, 1, .75F, .5F, .25F}),
                                    {&resample, 1});
    check(result.sample_rate == 24000 && result.frames() == 4,
          "offline resampling mismatch");
    auto invalid = mono({0, std::numeric_limits<float>::quiet_NaN()});
    bool rejected{};
    try { (void)process_sample_offline(invalid, {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "NaN input was accepted");

    const auto folder = std::filesystem::temp_directory_path() /
                        "neurogenic-sample-processing-tests";
    std::error_code ignored; std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);
    for (const auto format : {SampleWavFormat::pcm16, SampleWavFormat::pcm24,
                              SampleWavFormat::float32}) {
        const auto path = folder / (std::to_string(static_cast<int>(format)) + ".wav");
        save_sample_wav_atomic(mono({-.5F, 0, .5F}), path, format);
        const auto decoded = decode_wav(path);
        check(decoded.frames() == 3 && decoded.channels == 1,
              "WAV format round trip failed");
        rejected = false;
        try { save_sample_wav_atomic(mono({0}), path, format); }
        catch (const std::runtime_error &) { rejected = true; }
        check(rejected, "existing asset was silently overwritten");
    }

    AudioEngine engine(48000, 8);
    SampleManager manager(engine);
    SliceService slices(engine, manager);
    SampleProcessingService service(manager, slices);
    auto source = std::make_shared<const Sample>(
        Sample{48000, 1, std::vector<float>(128, .25F)});
    check(manager.publish_prepared(SampleSlotId{0}, source, "source.wav"),
          "source publication failed");
    std::array<float, 16> left{}, right{}; engine.process(left, right);
    auto slice_state = slices.snapshot(SampleSlotId{0});
    SliceId slice{}; RegionId region{}; std::string error;
    check(slices.create(SampleSlotId{0}, slice_state.revision, 16, 48,
                        "Slice", slice, error), "slice creation failed");
    slice_state = slices.snapshot(SampleSlotId{0});
    check(slices.create_region(SampleSlotId{0}, slice_state.revision, 32, 64,
                               "Region", region, error), "region creation failed");
    slice_state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_gain(region, slice_state.revision, 2, error),
          "region gain failed");
    slice_state = slices.snapshot(SampleSlotId{0});
    check(slices.set_region_loop(region, slice_state.revision, true, 40, 56, 0,
                                 error), "region loop failed");

    SampleProcessingJobId job{}; SampleProcessingOperationId operation{};
    check(service.create({SampleProcessingSourceKind::region, SampleSlotId{0},
                          region, 0, 80, true}, job, error),
          "region processing job creation failed");
    gain.value = -6.020599913;
    check(service.add_operation(job, gain, operation, error) &&
              service.validate(job, error) && service.start(job, error),
          "region job setup failed");
    wait(service, job);
    auto job_state = service.snapshot(job);
    check(job_state->status == SampleProcessingStatus::complete &&
              job_state->result.frames == 80,
          "looped region bounce length mismatch");
    const auto bounced = folder / "region.wav";
    check(service.save(job, bounced, SampleWavFormat::float32, false, error),
          "processed asset save failed");
    near(decode_wav(bounced).interleaved[0], .25F, .0001F,
         "region gain and processing gain were not composed");
    check(service.publish_to_slot(job, SampleSlotId{1}, error),
          "processed sample publication failed");
    check(service.snapshot(job)->status == SampleProcessingStatus::publication_pending,
          "publication reported success before callback acknowledgement");
    engine.process(left, right);
    check(service.snapshot(job)->status == SampleProcessingStatus::audio_active &&
              manager.status(SampleSlotId{1}).has_sample,
          "processed sample did not become audio active");

    SampleProcessingJobId slice_job{};
    check(service.create({SampleProcessingSourceKind::slice, SampleSlotId{0},
                          0, slice}, slice_job, error) &&
              service.start(slice_job, error), "slice bounce start failed");
    wait(service, slice_job);
    check(service.snapshot(slice_job)->result.frames == 32,
          "slice bounce range mismatch");

    SampleProcessingJobId cancelled_job{};
    check(service.create({SampleProcessingSourceKind::sample, SampleSlotId{0}},
                         cancelled_job, error) &&
              service.cancel(cancelled_job, error) &&
              service.snapshot(cancelled_job)->status ==
                  SampleProcessingStatus::cancelled,
          "draft processing cancellation failed");

    const auto source_status = manager.status(SampleSlotId{0});
    check(engine.trigger_slice(SampleSlotId{0}, source_status.active_publication,
                               0, source->frames(), 1, 1),
          "old source voice trigger failed");
    engine.process({left.data(), 1}, {right.data(), 1});
    check(service.publish_to_slot(job, SampleSlotId{0}, error),
          "explicit source-slot replacement failed");
    left.fill(0); right.fill(0); engine.process(left, right);
    check(std::any_of(left.begin(), left.end(), [](float value) {
              return std::abs(value) > .01F;
          }), "active old-version voice did not survive processed replacement");

    std::filesystem::remove_all(folder, ignored);
    std::cout << "neurogenic.sample-processing-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n'; return 1;
}
