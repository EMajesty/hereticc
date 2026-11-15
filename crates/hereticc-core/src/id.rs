macro_rules! id_type {
    ($name:ident) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(pub u64);

        impl $name {
            #[must_use]
            pub const fn new(value: u64) -> Self {
                Self(value)
            }

            #[must_use]
            pub const fn get(self) -> u64 {
                self.0
            }
        }
    };
}

id_type!(PatternId);
id_type!(SceneId);
id_type!(EventId);
id_type!(SampleId);
id_type!(SliceId);
id_type!(VoiceToken);

/// One of the sixteen logical HERETICC tracks.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TrackId(u8);

impl TrackId {
    pub const COUNT: usize = 16;

    #[must_use]
    pub const fn new(index: u8) -> Option<Self> {
        if index < 16 { Some(Self(index)) } else { None }
    }

    #[must_use]
    pub const fn index(self) -> usize {
        self.0 as usize
    }

    #[must_use]
    pub const fn get(self) -> u8 {
        self.0
    }

    #[must_use]
    pub fn all() -> [Self; Self::COUNT] {
        core::array::from_fn(|index| Self(u8::try_from(index).unwrap_or_default()))
    }
}
