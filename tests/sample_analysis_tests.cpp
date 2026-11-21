#include "neurogenic/audio/sample_analysis.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
neurogenic::Sample make_sample(std::size_t frames, bool stereo = false) {
    neurogenic::Sample sample;
    sample.sample_rate = 48000;
    sample.channels = stereo ? 2 : 1;
    sample.interleaved.assign(frames * sample.channels, 0.0F);
    return sample;
}
void pulse(neurogenic::Sample &sample, std::size_t frame, float gain = 1.0F) {
    for (std::size_t n = frame; n < std::min(frame + 128, sample.frames()); ++n)
        for (std::uint16_t channel = 0; channel < sample.channels; ++channel)
            sample.interleaved[n * sample.channels + channel] =
                gain * std::exp(-static_cast<float>(n - frame) / 24.0F);
}
void wait_complete(neurogenic::SampleAnalysisService &service,
                   neurogenic::SampleAnalysisSessionId id) {
    for (int i = 0; i < 1000; ++i) {
        const auto state = service.snapshot(id);
        check(state.has_value(), "analysis session disappeared");
        if (state->status == neurogenic::SampleAnalysisStatus::complete ||
            state->status == neurogenic::SampleAnalysisStatus::stale)
            return;
        if (state->status == neurogenic::SampleAnalysisStatus::failed)
            throw std::runtime_error(state->error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("analysis timed out");
}
}

int main() try {
    using namespace neurogenic;
    auto drum = make_sample(48000, true);
    pulse(drum, 0); pulse(drum, 12000); pulse(drum, 24000, 0.4F);
    pulse(drum, 36000);
    SampleAnalysisSettings transient;
    transient.algorithm = SampleAnalysisAlgorithm::transients;
    transient.sensitivity = 0.15F;
    transient.minimum_separation_frames = 2000;
    auto detected = analyze_sample(drum, SampleSlotId{2}, 4, 9, transient);
    check(detected.source_publication == 9 && detected.channels == 2,
          "analysis lost source identity");
    check(detected.candidates.size() >= 3,
          "energy detector missed synthetic drum transients");
    check(std::abs(static_cast<long long>(detected.candidates[0].frame) -
                   12000LL) <= 256,
          "transient position exceeded hop tolerance");

    auto sustained = make_sample(24000);
    for (std::size_t i = 3000; i < 9000; ++i) sustained.interleaved[i] = 0.2F;
    for (std::size_t i = 15000; i < 21000; ++i) sustained.interleaved[i] = 0.2F;
    SampleAnalysisSettings quiet;
    quiet.algorithm = SampleAnalysisAlgorithm::quiet_regions;
    quiet.quiet_threshold_db = -40;
    quiet.quiet_hysteresis_db = 3;
    quiet.minimum_quiet_frames = 1000;
    const auto gaps = analyze_sample(sustained, SampleSlotId{0}, 1, 1, quiet);
    check(!gaps.candidates.empty(), "quiet detector found no sustained gap");

    SampleAnalysisSettings equal;
    equal.algorithm = SampleAnalysisAlgorithm::equal_divisions;
    equal.divisions = 7;
    const auto divided = analyze_sample(sustained, SampleSlotId{0}, 1, 1, equal);
    check(divided.suggested_regions.size() == 7 &&
              divided.suggested_regions.front().start_frame == 0 &&
              divided.suggested_regions.back().end_frame == sustained.frames(),
          "non-divisible equal slicing lost coverage");
    SampleAnalysisSettings grid;
    grid.algorithm = SampleAnalysisAlgorithm::tempo_grid;
    grid.source_bpm = 120;
    grid.beats_per_slice = 0.5;
    const auto gridded = analyze_sample(sustained, SampleSlotId{0}, 1, 1, grid);
    check(gridded.suggested_regions.size() == 2 &&
              gridded.suggested_regions.front().end_frame == 12000,
          "tempo grid did not use source-frame coordinates");

    AudioEngine engine(48000, 8);
    SampleManager manager(engine);
    SliceService slices(engine, manager);
    SampleAnalysisService service(manager, slices);
    auto owned = std::make_shared<const Sample>(drum);
    check(manager.publish_prepared(SampleSlotId{0}, owned, "synthetic.wav"),
          "sample publication failed");
    std::array<float, 1> left{}, right{};
    engine.process(left, right);
    check(manager.status(SampleSlotId{0}).has_sample,
          "sample publication was not acknowledged");
    SampleAnalysisSessionId session{};
    check(service.start(SampleSlotId{0}, equal, session,
                        detected.diagnostics),
          "analysis session start failed");
    wait_complete(service, session);
    auto state = service.snapshot(session);
    check(state && state->result.candidates.size() == 6,
          "analysis session result mismatch");
    SampleAnalysisCandidateId added{};
    std::string error;
    check(service.add_candidate(session, 1000, added, error),
          "candidate add failed");
    check(service.move_candidate(session, added, 2000, error),
          "candidate move failed");
    check(service.remove_candidate(session, added, error),
          "candidate remove failed");
    check(!service.apply(session,
                         {SampleAnalysisApplyTarget::regions,
                          SampleAnalysisApplyMode::replace, true, 12}, error),
          "pad-capacity failure was accepted");
    check(slices.snapshot(SampleSlotId{0}).regions.empty(),
          "failed apply partially changed regions");
    check(service.apply(session,
                        {SampleAnalysisApplyTarget::regions,
                         SampleAnalysisApplyMode::replace, false, 0}, error),
          "region analysis application failed");
    check(slices.snapshot(SampleSlotId{0}).regions.size() == 7,
          "analysis regions were not applied atomically");
    check(service.apply(session,
                        {SampleAnalysisApplyTarget::slices,
                         SampleAnalysisApplyMode::replace, true, 0}, error),
          "atomic slice/pad application failed");
    engine.process(left, right);
    const auto mapped = slices.snapshot(SampleSlotId{0});
    check(mapped.slices.size() == 7 && mapped.pad_assignments[0].has_value() &&
              mapped.pad_assignments[6].has_value(),
          "analysis pad mapping batch was not installed");

    auto replacement = std::make_shared<const Sample>(make_sample(12000));
    check(manager.publish_prepared(SampleSlotId{0}, replacement,
                                   "replacement.wav"),
          "replacement publication failed");
    engine.process(left, right);
    state = service.snapshot(session);
    check(state && state->status == SampleAnalysisStatus::stale &&
              !service.apply(session, {}, error),
          "stale source analysis remained applicable");
    check(service.discard(session, error), "analysis discard failed");

    auto long_source = std::make_shared<const Sample>(make_sample(2'000'000));
    check(manager.publish_prepared(SampleSlotId{1}, long_source, "long.wav"),
          "long sample publication failed");
    engine.process(left, right);
    SampleAnalysisSessionId cancelled{};
    check(service.start(SampleSlotId{1}, transient, cancelled, error) &&
              service.cancel(cancelled, error),
          "analysis cancellation request failed");
    for (int i = 0; i < 1000; ++i) {
        const auto cancelled_state = service.snapshot(cancelled);
        check(cancelled_state.has_value(), "cancelled session disappeared");
        if (cancelled_state->status == SampleAnalysisStatus::cancelled) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(service.snapshot(cancelled)->status == SampleAnalysisStatus::cancelled,
          "analysis did not acknowledge cancellation");

    std::cout << "neurogenic.sample-analysis-tests.v1 passed\n";
    return 0;
} catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
}
