//! Minimal versioned binary persistence for the platform-neutral project model.
//!
//! The format is intentionally internal to M0.2. It is deterministic,
//! little-endian, and dependency-free, but compatibility beyond version 1 is
//! not promised until the project schema matures.

use crate::error::{CoreError, ValidationError};
use crate::id::{EventId, PatternId, SampleId, SceneId, SliceId, TrackId};
use crate::model::{PROJECT_FORMAT_VERSION, Pattern, Project, Scene, SoundSource, StepEvent};
use crate::parameter::{
    MacroId, ParameterAddress, ParameterChange, ParameterEvent, ParameterId, ParameterIntent,
    ParameterOwner, ParameterScope, PerformanceEffectId, ProcessorSlot, SendId,
};
use crate::time::{PatternLength, PatternTick};

const MAGIC: &[u8; 8] = b"HERETICC";

/// Encode a validated project using the M0.2 platform-neutral binary format.
///
/// # Errors
///
/// Returns an error for invalid project data or lengths that cannot be encoded.
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
    Ok(writer.bytes)
}

/// Decode and validate an M0.2 platform-neutral project.
///
/// # Errors
///
/// Returns an error for truncated, malformed, unsupported, or invalid data.
pub fn decode(bytes: &[u8]) -> Result<Project, CoreError> {
    let mut reader = Reader { bytes, offset: 0 };
    if reader.take(MAGIC.len())? != MAGIC {
        return Err(ValidationError::InvalidEncoding("project magic").into());
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
        track.patterns = (0..reader.count()?)
            .map(|_| reader.u64().map(PatternId::new))
            .collect::<Result<Vec<_>, _>>()?;
    }
    project.patterns = (0..reader.count()?)
        .map(|_| reader.pattern())
        .collect::<Result<Vec<_>, _>>()?;
    project.scenes = (0..reader.count()?)
        .map(|_| reader.scene())
        .collect::<Result<Vec<_>, _>>()?;
    if reader.offset != bytes.len() {
        return Err(ValidationError::TrailingData.into());
    }
    project.validate()?;
    Ok(project)
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
}

impl Reader<'_> {
    fn take(&mut self, count: usize) -> Result<&[u8], CoreError> {
        let end = self
            .offset
            .checked_add(count)
            .ok_or(CoreError::ArithmeticOverflow)?;
        let output = self
            .bytes
            .get(self.offset..end)
            .ok_or(CoreError::TruncatedProjectData)?;
        self.offset = end;
        Ok(output)
    }

    fn u8(&mut self) -> Result<u8, CoreError> {
        Ok(self.take(1)?[0])
    }

    fn u32(&mut self) -> Result<u32, CoreError> {
        Ok(u32::from_le_bytes(
            self.take(4)?.try_into().expect("four bytes"),
        ))
    }

    fn u64(&mut self) -> Result<u64, CoreError> {
        Ok(u64::from_le_bytes(
            self.take(8)?.try_into().expect("eight bytes"),
        ))
    }

    fn count(&mut self) -> Result<usize, CoreError> {
        usize::try_from(self.u32()?).map_err(|_| CoreError::ArithmeticOverflow)
    }

    fn string(&mut self) -> Result<String, CoreError> {
        let length = self.count()?;
        let value = core::str::from_utf8(self.take(length)?)
            .map_err(|_| ValidationError::InvalidEncoding("parameter utf-8"))?;
        Ok(value.to_owned())
    }

    fn track(&mut self) -> Result<TrackId, CoreError> {
        TrackId::new(self.u8()?).ok_or_else(|| ValidationError::InvalidEncoding("track id").into())
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
                    _ => return Err(ValidationError::InvalidEncoding("slice flag").into()),
                };
                Ok(SoundSource::Sample { sample, slice })
            }
            _ => Err(ValidationError::InvalidEncoding("sound source").into()),
        }
    }

    fn pattern(&mut self) -> Result<Pattern, CoreError> {
        let id = PatternId::new(self.u64()?);
        let track = self.track()?;
        let length = PatternLength::new(self.u64()?)
            .ok_or(ValidationError::InvalidEncoding("zero pattern length"))?;
        let notes = (0..self.count()?)
            .map(|_| {
                let id = EventId::new(self.u64()?);
                let at = PatternTick::new(self.u64()?);
                let duration_ticks = self.u64()?;
                let sound = self.sound()?;
                let parameters = (0..self.count()?)
                    .map(|_| self.change())
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(StepEvent {
                    id,
                    at,
                    duration_ticks,
                    sound,
                    parameters,
                })
            })
            .collect::<Result<Vec<_>, CoreError>>()?;
        let parameters = (0..self.count()?)
            .map(|_| {
                Ok(ParameterEvent {
                    id: EventId::new(self.u64()?),
                    at: PatternTick::new(self.u64()?),
                    change: self.change()?,
                })
            })
            .collect::<Result<Vec<_>, CoreError>>()?;
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
            index => Some(
                ProcessorSlot::new(index)
                    .ok_or(ValidationError::InvalidEncoding("processor slot"))?,
            ),
        };
        let parameter = ParameterId::new(self.string()?)
            .ok_or(ValidationError::InvalidEncoding("parameter id"))?;
        let value_tag = self.u8()?;
        let value = f64::from_bits(self.u64()?);
        let intent = match value_tag {
            0 => ParameterIntent::Absolute(value),
            1 => ParameterIntent::Relative(value),
            _ => return Err(ValidationError::InvalidEncoding("parameter intent").into()),
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
                    u8::try_from(value)
                        .map_err(|_| ValidationError::InvalidEncoding("track owner"))?,
                )
                .ok_or(ValidationError::InvalidEncoding("track owner"))?,
            )),
            2 => Ok(ParameterOwner::Send(SendId::new(value))),
            3 if value == 0 => Ok(ParameterOwner::Master),
            4 => Ok(ParameterOwner::PerformanceEffect(PerformanceEffectId::new(
                value,
            ))),
            5 => Ok(ParameterOwner::Macro(MacroId::new(value))),
            _ => Err(ValidationError::InvalidEncoding("parameter owner").into()),
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
        _ => Err(ValidationError::InvalidEncoding("parameter scope").into()),
    }
}
