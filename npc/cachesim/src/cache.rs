use std::collections::{HashSet, VecDeque};

use anyhow::{Result, bail};
use serde::Serialize;

use crate::replacement::ReplacementPolicy;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RefillMode {
    IndependentWord,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CacheAccessKind {
    Hit,
    Miss,
    Bypass,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MissKind3C {
    Compulsory,
    Capacity,
    Conflict,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CacheConfig {
    pub block_bytes: u32,
    pub total_lines: u32,
    pub ways: u32,
    pub replacement: ReplacementPolicy,
}

impl CacheConfig {
    pub fn validate(self) -> Result<Self> {
        if self.block_bytes < 4 || !self.block_bytes.is_power_of_two() || self.block_bytes % 4 != 0 {
            bail!("block_bytes must be a power of two, >= 4, and a multiple of 4");
        }
        if self.total_lines == 0 || !self.total_lines.is_power_of_two() {
            bail!("lines must be a positive power of two");
        }
        if self.ways == 0 || self.total_lines % self.ways != 0 {
            bail!("ways must be positive and divide total_lines");
        }
        let sets = self.total_lines / self.ways;
        if !sets.is_power_of_two() {
            bail!("sets must be a power of two");
        }
        Ok(self)
    }

    pub fn sets(self) -> u32 {
        self.total_lines / self.ways
    }

    pub fn words_per_line(self) -> u32 {
        self.block_bytes / 4
    }

    pub fn capacity_bytes(self) -> u32 {
        self.block_bytes * self.total_lines
    }
}

#[derive(Debug, Clone)]
struct CacheLine {
    valid: bool,
    tag: u32,
    inserted_at: u64,
    last_used_at: u64,
}

impl Default for CacheLine {
    fn default() -> Self {
        Self {
            valid: false,
            tag: 0,
            inserted_at: 0,
            last_used_at: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct CacheLineSnapshot {
    pub valid: bool,
    pub tag: u32,
}

#[derive(Debug, Clone)]
pub struct CacheModelSnapshot {
    pub sets: Vec<Vec<CacheLineSnapshot>>,
}

#[derive(Debug, Clone)]
struct ShadowFaLru {
    capacity_lines: usize,
    recency: VecDeque<u64>,
}

impl ShadowFaLru {
    fn new(capacity_lines: usize) -> Self {
        Self {
            capacity_lines,
            recency: VecDeque::new(),
        }
    }

    fn contains(&self, block: u64) -> bool {
        self.recency.contains(&block)
    }

    fn access(&mut self, block: u64) {
        if let Some(idx) = self.recency.iter().position(|entry| *entry == block) {
            self.recency.remove(idx);
        } else if self.recency.len() == self.capacity_lines {
            self.recency.pop_back();
        }
        self.recency.push_front(block);
    }
}

#[derive(Debug, Clone)]
pub struct CacheAccessResult {
    pub kind: CacheAccessKind,
    pub block_number: Option<u64>,
    pub miss_3c: Option<MissKind3C>,
    pub refill_words: u64,
}

#[derive(Debug, Clone)]
pub struct CacheModel {
    config: CacheConfig,
    sets: Vec<Vec<CacheLine>>,
    timestamp: u64,
    seen_blocks: HashSet<u64>,
    shadow_fa: ShadowFaLru,
}

impl CacheModel {
    pub fn new(config: CacheConfig) -> Self {
        let config = config.validate().unwrap();
        let sets = (0..config.sets())
            .map(|_| vec![CacheLine::default(); config.ways as usize])
            .collect();
        Self {
            config,
            sets,
            timestamp: 0,
            seen_blocks: HashSet::new(),
            shadow_fa: ShadowFaLru::new(config.total_lines as usize),
        }
    }

    pub fn config(&self) -> CacheConfig {
        self.config
    }

    pub fn refill_mode(&self) -> RefillMode {
        RefillMode::IndependentWord
    }

    pub fn snapshot(&self) -> CacheModelSnapshot {
        CacheModelSnapshot {
            sets: self
                .sets
                .iter()
                .map(|set| {
                    set.iter()
                        .map(|line| CacheLineSnapshot {
                            valid: line.valid,
                            tag: line.tag,
                        })
                        .collect()
                })
                .collect(),
        }
    }

    pub fn access(&mut self, pc: u32, cacheable: bool) -> CacheAccessResult {
        if !cacheable {
            return CacheAccessResult {
                kind: CacheAccessKind::Bypass,
                block_number: None,
                miss_3c: None,
                refill_words: 0,
            };
        }

        self.timestamp += 1;
        let block_number = (pc / self.config.block_bytes) as u64;
        let set_index = (block_number & ((self.config.sets() - 1) as u64)) as usize;
        let tag = (block_number / self.config.sets() as u64) as u32;
        let set = &mut self.sets[set_index];

        if let Some((way_idx, _line)) = set
            .iter_mut()
            .enumerate()
            .find(|(_, line)| line.valid && line.tag == tag)
        {
            if self.config.replacement == ReplacementPolicy::Lru {
                set[way_idx].last_used_at = self.timestamp;
            }
            self.shadow_fa.access(block_number);
            return CacheAccessResult {
                kind: CacheAccessKind::Hit,
                block_number: Some(block_number),
                miss_3c: None,
                refill_words: 0,
            };
        }

        let miss_3c = if self.seen_blocks.insert(block_number) {
            MissKind3C::Compulsory
        } else if self.shadow_fa.contains(block_number) {
            MissKind3C::Conflict
        } else {
            MissKind3C::Capacity
        };

        let victim_idx = if let Some((idx, _)) = set.iter().enumerate().find(|(_, line)| !line.valid) {
            idx
        } else {
            match self.config.replacement {
                ReplacementPolicy::Lru => set
                    .iter()
                    .enumerate()
                    .min_by_key(|(idx, line)| (line.last_used_at, *idx))
                    .map(|(idx, _)| idx)
                    .unwrap(),
                ReplacementPolicy::Fifo => set
                    .iter()
                    .enumerate()
                    .min_by_key(|(idx, line)| (line.inserted_at, *idx))
                    .map(|(idx, _)| idx)
                    .unwrap(),
            }
        };

        set[victim_idx] = CacheLine {
            valid: true,
            tag,
            inserted_at: self.timestamp,
            last_used_at: self.timestamp,
        };
        self.shadow_fa.access(block_number);

        CacheAccessResult {
            kind: CacheAccessKind::Miss,
            block_number: Some(block_number),
            miss_3c: Some(miss_3c),
            refill_words: self.config.words_per_line() as u64,
        }
    }
}
