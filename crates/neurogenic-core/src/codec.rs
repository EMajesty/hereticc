//! Bounded, versioned binary persistence for the platform-neutral project model.
//!
//! The provisional version-1 format is deterministic, little-endian, and
//! dependency-free. Decoding applies explicit resource limits before any
//! allocation derived from untrusted count or length fields.

use core::mem::size_of;

use crate::error::{CoreError, DecodeError, DecodeLimit, ValidationError};
use crate::id::{EventId, PatternId, SampleId, SceneId, SliceId, TrackId};
use crate::model::{PROJECT_FORMAT_VERSION, Pattern, Project, Scene, SoundSource, StepEvent};
use crate::parameter::{
    MAX_PARAMETER_ID_BYTES, MacroId, ParameterAddress, ParameterChange, ParameterEvent,
    ParameterId, ParameterIntent, ParameterOwner, ParameterScope, PerformanceEffectId,
    ProcessorSlot, SendId,
};
use crate::time::{PatternLength, PatternTick};

const MAGIC: &[u8; 8] = b"HERETICC";

/// Resource policy for decoding untrusted version-1 project data.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DecodeLimits {
    pub max_encoded_project_bytes: usize,
    pub max_patterns: usize,
    pub max_scenes: usize,
    pub max_track_pattern_references: usize,
    pub max_events_per_pattern: usize,
    pub max_events_per_project: usize,
    pub max_parameter_changes_per_event: usize,
    pub max_parameter_identifier_bytes: usize,
    pub max_decoded_allocation_bytes: usize,
}

impl Default for DecodeLimits {
    fn default() -> Self {
        Self {
            max_encoded_project_bytes: 16 * 1024 * 1024,
            max_patterns: 4_096,
            max_scenes: 1_024,
            max_track_pattern_references: 4_096,
            max_events_per_pattern: 65_536,
            max_events_per_project: 262_144,
            max_parameter_changes_per_event: 256,
            max_parameter_identifier_bytes: MAX_PARAMETER_ID_BYTES,
            max_decoded_allocation_bytes: 64 * 1024 * 1024,
        }
    }
}

/// Encode a validated project using the provisional version-1 format.
///
/// # Errors
///
/// Returns an error for invalid project data, unencodable lengths, or output
/// beyond the default encoded-project size policy.
pub fn encode(project: &Project) -> Result<Vec<u8>, CoreError> {
    project.validate()?;
    let mut writer = Writer::default();
    writer.bytes.extend_from_slice(MAGIC);
    writer.u32(project.format_version);
    writer.u64(project.initial_scene.get());
    for track in &project.tracks {
        writer.sound(track.default_sound);
        writer.count(track.patterns.len())?;
        for pattern in &track.patterns {
            writer.u64(pattern.get());
        }
    }
    writer.count(project.patterns.len())?;
    for pattern in &project.patterns {
        writer.pattern(pattern)?;
    }
    writer.count(project.scenes.len())?;
    for scene in &project.scenes {
        writer.u64(scene.id.get());
        for pattern in scene.patterns {
            writer.u64(pattern.get());
        }
    }
    let maximum = DecodeLimits::default().max_encoded_project_bytes;
    if writer.bytes.len() > maximum {
        return Err(limit_error(
            DecodeLimit::EncodedProjectBytes,
            writer.bytes.len(),
            maximum,
        ));
    }
    Ok(writer.bytes)
}

/// Decode using the centrally defined default resource policy.
///
/// # Errors
///
/// Returns a structured error for truncated, malformed, oversized, unsupported,
/// or semantically invalid project data.
pub fn decode(bytes: &[u8]) -> Result<Project, CoreError> {
    decode_with_limits(bytes, DecodeLimits::default())
}

/// Decode using an explicit resource policy.
///
/// # Errors
///
/// Returns a structured error before allocation when a declared size exceeds
/// `limits`, and validates all persistent references after parsing.
pub fn decode_with_limits(bytes: &[u8], limits: DecodeLimits) -> Result<Project, CoreError> {
    if bytes.len() > limits.max_encoded_project_bytes {
        return Err(limit_error(
            DecodeLimit::EncodedProjectBytes,
            bytes.len(),
            limits.max_encoded_project_bytes,
        ));
    }
    let mut reader = Reader::new(bytes, limits);
    if reader.take(MAGIC.len())? != MAGIC {
        return Err(DecodeError::InvalidMagic.into());
    }
    let format_version = reader.u32()?;
    if format_version != PROJECT_FORMAT_VERSION {
        return Err(ValidationError::UnsupportedFormatVersion(format_version).into());
    }
    let initial_scene = SceneId::new(reader.u64()?);
    let mut project = Project::empty(initial_scene);
    project.format_version = format_version;
    for track in &mut project.tracks {
        track.default_sound = reader.sound()?;
        let count = reader.bounded_count(
            DecodeLimit::TrackPatternReferences,
            limits.max_track_pattern_references,
            size_of::<PatternId>(),
        )?;
        let mut patterns = Vec::with_capacity(count);
        for _ in 0..count {
            patterns.push(PatternId::new(reader.u64()?));
        }
        track.patterns = patterns;
    }
    let pattern_count = reader.bounded_count(
        DecodeLimit::Patterns,
        limits.max_patterns,
        size_of::<Pattern>(),
    )?;
    project.patterns = Vec::with_capacity(pattern_count);
    for _ in 0..pattern_count {
        project.patterns.push(reader.pattern()?);
    }
    let scene_count =
        reader.bounded_count(DecodeLimit::Scenes, limits.max_scenes, size_of::<Scene>())?;
    project.scenes = Vec::with_capacity(scene_count);
    for _ in 0..scene_count {
        project.scenes.push(reader.scene()?);
    }
    if reader.offset != bytes.len() {
        return Err(DecodeError::TrailingData {
            bytes: bytes.len() - reader.offset,
        }
        .into());
    }
    project.validate()?;
    Ok(project)
}

fn limit_error(limit: DecodeLimit, declared: usize, maximum: usize) -> CoreError {
    CoreError::Decode(DecodeError::LimitExceeded {
        limit,
        declared: u64::try_from(declared).unwrap_or(u64::MAX),
        maximum: u64::try_from(maximum).unwrap_or(u64::MAX),
    })
}

#[derive(Default)]
struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    fn u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    fn u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    fn u64(&mut self, value: u64) {
        self.bytes.extend_from_slice(&value.to_le_bytes());
    }

    fn count(&mut self, value: usize) -> Result<(), CoreError> {
        self.u32(u32::try_from(value).map_err(|_| CoreError::ArithmeticOverflow)?);
        Ok(())
    }

    fn string(&mut self, value: &str) -> Result<(), CoreError> {
        self.count(value.len())?;
        self.bytes.extend_from_slice(value.as_bytes());
        Ok(())
    }

    fn sound(&mut self, sound: SoundSource) {
        match sound {
            SoundSource::TrackDefault => self.u8(0),
            SoundSource::Pad(track) => {
                self.u8(1);
                self.u8(track.get());
            }
            SoundSource::Sample { sample, slice } => {
                self.u8(2);
                self.u64(sample.get());
                self.u8(u8::from(slice.is_some()));
                if let Some(slice) = slice {
                    self.u64(slice.get());
                }
            }
        }
    }

    fn pattern(&mut self, pattern: &Pattern) -> Result<(), CoreError> {
        self.u64(pattern.id.get());
        self.u8(pattern.track.get());
        self.u64(pattern.length.ticks());
        self.count(pattern.notes.len())?;
        for note in &pattern.notes {
            self.u64(note.id.get());
            self.u64(note.at.ticks());
            self.u64(note.duration_ticks);
            self.sound(note.sound);
            self.count(note.parameters.len())?;
            for change in &note.parameters {
                self.change(change)?;
            }
        }
        self.count(pattern.parameters.len())?;
        for event in &pattern.parameters {
            self.u64(event.id.get());
            self.u64(event.at.ticks());
            self.change(&event.change)?;
        }
        Ok(())
    }

    fn change(&mut self, change: &ParameterChange) -> Result<(), CoreError> {
        self.u8(scope_tag(change.address.scope));
        self.owner(change.address.owner);
        self.u8(change
            .address
            .processor_slot
            .map_or(u8::MAX, ProcessorSlot::index));
        self.string(change.address.parameter.as_str())?;
        match change.intent {
            ParameterIntent::Absolute(value) => {
                self.u8(0);
                self.u64(value.to_bits());
            }
            ParameterIntent::Relative(value) => {
                self.u8(1);
                self.u64(value.to_bits());
            }
        }
        Ok(())
    }

    fn owner(&mut self, owner: ParameterOwner) {
        match owner {
            ParameterOwner::EventVoice(id) => {
                self.u8(0);
                self.u64(id.get());
            }
            ParameterOwner::Track(id) => {
                self.u8(1);
                self.u64(u64::from(id.get()));
            }
            ParameterOwner::Send(id) => {
                self.u8(2);
                self.u64(id.get());
            }
            ParameterOwner::Master => {
                self.u8(3);
                self.u64(0);
            }
            ParameterOwner::PerformanceEffect(id) => {
                self.u8(4);
                self.u64(id.get());
            }
            ParameterOwner::Macro(id) => {
                self.u8(5);
                self.u64(id.get());
            }
        }
    }
}

struct Reader<'bytes> {
    bytes: &'bytes [u8],
    offset: usize,
    limits: DecodeLimits,
    remaining_allocation: usize,
    total_events: usize,
}

impl<'bytes> Reader<'bytes> {
    fn new(bytes: &'bytes [u8], limits: DecodeLimits) -> Self {
        Self {
            bytes,
            offset: 0,
            limits,
            remaining_allocation: limits.max_decoded_allocation_bytes,
            total_events: 0,
        }
    }

    fn take(&mut self, count: usize) -> Result<&'bytes [u8], CoreError> {
        let end = self
            .offset
            .checked_add(count)
            .ok_or(DecodeError::LengthOverflow)?;
        let output = self
            .bytes
            .get(self.offset..end)
            .ok_or(DecodeError::Truncated)?;
        self.offset = end;
        Ok(output)
    }

    fn charge(&mut self, count: usize, element_size: usize) -> Result<(), CoreError> {
        let requested = count
            .checked_mul(element_size)
            .ok_or(DecodeError::LengthOverflow)?;
        let already_charged = self
            .limits
            .max_decoded_allocation_bytes
            .checked_sub(self.remaining_allocation)
            .ok_or(DecodeError::LengthOverflow)?;
        let total = already_charged
            .checked_add(requested)
            .ok_or(DecodeError::LengthOverflow)?;
        if total > self.limits.max_decoded_allocation_bytes {
            return Err(limit_error(
                DecodeLimit::DecodedAllocationBytes,
                total,
                self.limits.max_decoded_allocation_bytes,
            ));
        }
        self.remaining_allocation = self.limits.max_decoded_allocation_bytes - total;
        Ok(())
    }

    fn bounded_count(
        &mut self,
        kind: DecodeLimit,
        maximum: usize,
        element_size: usize,
    ) -> Result<usize, CoreError> {
        let count = usize::try_from(self.u32()?).map_err(|_| DecodeError::LengthOverflow)?;
        if count > maximum {
            return Err(limit_error(kind, count, maximum));
        }
        self.charge(count, element_size)?;
        Ok(count)
    }

    fn add_events(&mut self, count: usize) -> Result<(), CoreError> {
        self.total_events = self
            .total_events
            .checked_add(count)
            .ok_or(DecodeError::LengthOverflow)?;
        if self.total_events > self.limits.max_events_per_project {
            return Err(limit_error(
                DecodeLimit::EventsPerProject,
                self.total_events,
                self.limits.max_events_per_project,
            ));
        }
        Ok(())
    }

    fn u8(&mut self) -> Result<u8, CoreError> {
        Ok(self.take(1)?[0])
    }

    fn u32(&mut self) -> Result<u32, CoreError> {
        let mut bytes = [0; 4];
        bytes.copy_from_slice(self.take(4)?);
        Ok(u32::from_le_bytes(bytes))
    }

    fn u64(&mut self) -> Result<u64, CoreError> {
        let mut bytes = [0; 8];
        bytes.copy_from_slice(self.take(8)?);
        Ok(u64::from_le_bytes(bytes))
    }

    fn string(&mut self) -> Result<String, CoreError> {
        let length = usize::try_from(self.u32()?).map_err(|_| DecodeError::LengthOverflow)?;
        if length > self.limits.max_parameter_identifier_bytes {
            return Err(limit_error(
                DecodeLimit::ParameterIdentifierBytes,
                length,
                self.limits.max_parameter_identifier_bytes,
            ));
        }
        self.charge(length, 1)?;
        let value =
            core::str::from_utf8(self.take(length)?).map_err(|_| DecodeError::InvalidUtf8)?;
        Ok(value.to_owned())
    }

    fn track(&mut self) -> Result<TrackId, CoreError> {
        TrackId::new(self.u8()?).ok_or_else(|| DecodeError::InvalidTag("track id").into())
    }

    fn sound(&mut self) -> Result<SoundSource, CoreError> {
        match self.u8()? {
            0 => Ok(SoundSource::TrackDefault),
            1 => Ok(SoundSource::Pad(self.track()?)),
            2 => {
                let sample = SampleId::new(self.u64()?);
                let slice = match self.u8()? {
                    0 => None,
                    1 => Some(SliceId::new(self.u64()?)),
                    _ => return Err(DecodeError::InvalidTag("slice flag").into()),
                };
                Ok(SoundSource::Sample { sample, slice })
            }
            _ => Err(DecodeError::InvalidTag("sound source").into()),
        }
    }

    fn pattern(&mut self) -> Result<Pattern, CoreError> {
        let id = PatternId::new(self.u64()?);
        let track = self.track()?;
        let length = PatternLength::new(self.u64()?)
            .ok_or(ValidationError::InvalidEncoding("zero pattern length"))?;
        let note_count = self.bounded_count(
            DecodeLimit::EventsPerPattern,
            self.limits.max_events_per_pattern,
            size_of::<StepEvent>(),
        )?;
        self.add_events(note_count)?;
        let mut notes = Vec::with_capacity(note_count);
        for _ in 0..note_count {
            let event_id = EventId::new(self.u64()?);
            let at = PatternTick::new(self.u64()?);
            let duration_ticks = self.u64()?;
            let sound = self.sound()?;
            let parameter_count = self.bounded_count(
                DecodeLimit::ParameterChangesPerEvent,
                self.limits.max_parameter_changes_per_event,
                size_of::<ParameterChange>(),
            )?;
            let mut parameters = Vec::with_capacity(parameter_count);
            for _ in 0..parameter_count {
                parameters.push(self.change()?);
            }
            notes.push(StepEvent {
                id: event_id,
                at,
                duration_ticks,
                sound,
                parameters,
            });
        }
        let remaining_pattern_events = self.limits.max_events_per_pattern - note_count;
        let parameter_count = self.bounded_count(
            DecodeLimit::EventsPerPattern,
            remaining_pattern_events,
            size_of::<ParameterEvent>(),
        )?;
        self.add_events(parameter_count)?;
        let mut parameters = Vec::with_capacity(parameter_count);
        for _ in 0..parameter_count {
            parameters.push(ParameterEvent {
                id: EventId::new(self.u64()?),
                at: PatternTick::new(self.u64()?),
                change: self.change()?,
            });
        }
        Ok(Pattern {
            id,
            track,
            length,
            notes,
            parameters,
        })
    }

    fn scene(&mut self) -> Result<Scene, CoreError> {
        let id = SceneId::new(self.u64()?);
        let mut patterns = [PatternId::new(0); TrackId::COUNT];
        for pattern in &mut patterns {
            *pattern = PatternId::new(self.u64()?);
        }
        Ok(Scene { id, patterns })
    }

    fn change(&mut self) -> Result<ParameterChange, CoreError> {
        let scope = scope_from_tag(self.u8()?)?;
        let owner = self.owner()?;
        let processor_slot = match self.u8()? {
            u8::MAX => None,
            index => {
                Some(ProcessorSlot::new(index).ok_or(DecodeError::InvalidTag("processor slot"))?)
            }
        };
        let parameter = ParameterId::new(self.string()?)
            .ok_or(ValidationError::InvalidEncoding("parameter id"))?;
        let value_tag = self.u8()?;
        let value = f64::from_bits(self.u64()?);
        let intent = match value_tag {
            0 => ParameterIntent::Absolute(value),
            1 => ParameterIntent::Relative(value),
            _ => return Err(DecodeError::InvalidTag("parameter intent").into()),
        };
        Ok(ParameterChange {
            address: ParameterAddress {
                scope,
                owner,
                processor_slot,
                parameter,
            },
            intent,
        })
    }

    fn owner(&mut self) -> Result<ParameterOwner, CoreError> {
        let tag = self.u8()?;
        let value = self.u64()?;
        match tag {
            0 => Ok(ParameterOwner::EventVoice(EventId::new(value))),
            1 => Ok(ParameterOwner::Track(
                TrackId::new(
                    u8::try_from(value).map_err(|_| DecodeError::InvalidTag("track owner"))?,
                )
                .ok_or(DecodeError::InvalidTag("track owner"))?,
            )),
            2 => Ok(ParameterOwner::Send(SendId::new(value))),
            3 if value == 0 => Ok(ParameterOwner::Master),
            4 => Ok(ParameterOwner::PerformanceEffect(PerformanceEffectId::new(
                value,
            ))),
            5 => Ok(ParameterOwner::Macro(MacroId::new(value))),
            _ => Err(DecodeError::InvalidTag("parameter owner").into()),
        }
    }
}

const fn scope_tag(scope: ParameterScope) -> u8 {
    match scope {
        ParameterScope::Voice => 0,
        ParameterScope::Track => 1,
        ParameterScope::TrackInsert => 2,
        ParameterScope::Send => 3,
        ParameterScope::SendEffect => 4,
        ParameterScope::Master => 5,
        ParameterScope::MasterInsert => 6,
        ParameterScope::PerformanceEffect => 7,
        ParameterScope::Macro => 8,
    }
}

fn scope_from_tag(tag: u8) -> Result<ParameterScope, CoreError> {
    match tag {
        0 => Ok(ParameterScope::Voice),
        1 => Ok(ParameterScope::Track),
        2 => Ok(ParameterScope::TrackInsert),
        3 => Ok(ParameterScope::Send),
        4 => Ok(ParameterScope::SendEffect),
        5 => Ok(ParameterScope::Master),
        6 => Ok(ParameterScope::MasterInsert),
        7 => Ok(ParameterScope::PerformanceEffect),
        8 => Ok(ParameterScope::Macro),
        _ => Err(DecodeError::InvalidTag("parameter scope").into()),
    }
}
