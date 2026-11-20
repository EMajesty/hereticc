#include "neurogenic/project/preset.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace neurogenic;

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

SoundPreset fixture(const std::filesystem::path &asset) {
    SoundPreset value;
    value.metadata.id = "complete-sound";
    value.metadata.name = "Complete Sound";
    value.metadata.author = "NEUROGENIC";
    value.metadata.description = "deterministic test preset";
    value.metadata.type = PresetType::track_sound;
    value.metadata.tags = {"test", "bass"};
    value.metadata.required_capabilities = {"native-synth", "native-fx"};
    value.metadata.processor_types = {"native.gain.v1"};
    value.metadata.modulator_types = {"native.lfo.v1"};
    value.synth_parameters = {{"cutoff", 0.375}, {"resonance", 0.2}};
    value.track_gain = 0.75F;
    value.track_pan = -0.25F;

    SliceProject samples;
    samples.sample_paths.emplace(1, asset);
    samples.slices.push_back({7, 1, 2, 3, 10, 90, "break", 60, 0.8F});
    samples.pads[0] = 7;
    samples.pad_settings[0].gain = 0.7F;
    value.samples = samples;

    PresetEffectChain effects;
    EffectInstanceConfiguration effect;
    effect.id = 11;
    effect.type = EffectProcessorType::gain;
    effect.parameter_count = 1;
    effect.parameters[0] = {1, 0.5F};
    effects.instances.push_back(effect);
    effects.chain.count = 1;
    effects.chain.instances[0] = effect.id;
    value.effects = effects;

    value.modulation.modulator_count = 1;
    value.modulation.modulators[0].id = 21;
    value.modulation.modulators[0].type = ModulatorType::lfo;
    value.modulation.modulators[0].owner_kind = ModulatorOwnerKind::track;
    value.modulation.modulators[0].owner_id = 1;
    return value;
}
} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("neurogenic-preset-tests-" + std::to_string(stamp));
    try {
        std::filesystem::create_directories(root);
        const auto asset = root / "break.wav";
        std::ofstream(asset, std::ios::binary).write("RIFF", 4);
        PresetLibrary library(root / "library");
        const auto source = fixture(asset);
        check(validate_preset(source).valid, "valid preset rejected");

        const auto started = std::chrono::steady_clock::now();
        library.save(source);
        const auto loaded = library.load(source.metadata.id);
        check(loaded.metadata.name == source.metadata.name &&
                  loaded.synth_parameters == source.synth_parameters,
              "metadata or synth round-trip");
        check(loaded.samples && loaded.samples->slices.size() == 1 &&
                  loaded.samples->pads[0] == 7,
              "sample instrument round-trip");
        check(loaded.effects && loaded.effects->instances.size() == 1 &&
                  loaded.effects->chain.instances[0] == 11,
              "effect chain round-trip");
        check(loaded.modulation.modulator_count == 1,
              "modulation round-trip");
        check(library.enumerate(PresetType::track_sound).size() == 1,
              "preset enumeration");

        EffectRackConfiguration base_effects;
        ModulationConfiguration base_modulation;
        PresetImportResult first, second;
        std::string error;
        check(prepare_preset_import(loaded, base_effects, base_modulation,
                                    first, error),
              error.c_str());
        check(first.effect_ids.at(11) != 11 && first.modulator_ids.at(21) != 21,
              "fresh runtime IDs");
        check(prepare_preset_import(loaded, first.effects, first.modulation,
                                    second, error),
              error.c_str());
        check(second.effect_ids.at(11) != first.effect_ids.at(11) &&
                  second.modulator_ids.at(21) != first.modulator_ids.at(21),
              "multiple preset instances collide");

        library.duplicate(source.metadata.id, "copy", "Copy");
        check(library.load("copy").metadata.name == "Copy", "duplicate");
        const auto package = root / "portable-package";
        library.export_package("copy", package);
        PresetLibrary imported(root / "imported");
        imported.import_package(package, "imported-copy");
        check(imported.load("imported-copy").effects.has_value(),
              "portable import");

        auto missing = source;
        missing.metadata.id = "missing";
        missing.samples->sample_paths[1] = root / "absent.wav";
        check(!validate_preset(missing, true).valid &&
                  validate_preset(missing, false).valid,
              "missing asset policy");
        library.save(missing);
        check(library.load("missing", false).samples.has_value(),
              "inspectable missing asset");

        auto invalid = source;
        invalid.metadata.id = "invalid";
        invalid.effects->instances[0].type =
            static_cast<EffectProcessorType>(99);
        check(!validate_preset(invalid, false).valid,
              "unsupported processor accepted");
        auto invalid_modulator = source;
        invalid_modulator.metadata.modulator_types = {"missing-modulator"};
        check(!validate_preset(invalid_modulator, false).valid,
              "unsupported modulator accepted");
        const auto before = first;
        check(!prepare_preset_import(invalid, first.effects, first.modulation,
                                     second, error) &&
                  before.effects.instance_count == first.effects.instance_count,
              "failed import changed active configuration");
        auto failed_save = source;
        failed_save.metadata.name = "must not replace";
        failed_save.synth_parameters["cutoff"] =
            std::numeric_limits<double>::quiet_NaN();
        try {
            library.save(failed_save);
            check(false, "invalid save unexpectedly succeeded");
        } catch (const std::invalid_argument &) {
        }
        check(library.load(source.metadata.id).metadata.name == source.metadata.name,
              "failed save replaced active generation");

        auto full_effects = EffectRackConfiguration{};
        full_effects.instance_count = full_effects.instances.size();
        check(!prepare_preset_import(loaded, full_effects, base_modulation,
                                     second, error) &&
                  error.find("capacity") != std::string::npos,
              "capacity exhaustion");

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        std::cout << "preset_control_us=" << elapsed
                  << " prepared_effects=" << first.effects.instance_count
                  << " prepared_modulators=" << first.modulation.modulator_count
                  << '\n';
        std::filesystem::remove_all(root);
        std::cout << "neurogenic.preset-tests.v1 passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::filesystem::remove_all(root);
        std::cerr << "preset test failure: " << exception.what() << '\n';
        return 1;
    }
}
