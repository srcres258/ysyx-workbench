use std::collections::{BTreeMap, HashMap, HashSet};

use anyhow::{Result, bail};
use log::debug;
use serde::Serialize;

use crate::cache::{CacheAccessKind, CacheConfig};
use crate::machine::ClassifiedAddress;

#[derive(Debug, Clone, Copy, Default, Serialize)]
pub struct Miss3C {
    pub compulsory: u64,
    pub capacity: u64,
    pub conflict: u64,
}

#[derive(Debug, Clone, Default)]
pub struct RegionAccumulator {
    pub requests: u64,
    pub hits: u64,
    pub misses: u64,
    pub bypasses: u64,
    pub miss_3c: Miss3C,
    unique_pcs: HashSet<u32>,
    unique_blocks: HashSet<u64>,
}

impl RegionAccumulator {
    pub fn finalize(self) -> RegionStats {
        RegionStats {
            requests: self.requests,
            hits: self.hits,
            misses: self.misses,
            bypasses: self.bypasses,
            unique_pcs: self.unique_pcs.len() as u64,
            unique_cache_blocks: self.unique_blocks.len() as u64,
            miss_3c: self.miss_3c,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct RegionStats {
    pub requests: u64,
    pub hits: u64,
    pub misses: u64,
    pub bypasses: u64,
    pub unique_pcs: u64,
    pub unique_cache_blocks: u64,
    pub miss_3c: Miss3C,
}

#[derive(Debug, Clone, Serialize)]
pub struct ExactMetrics {
    pub requests: u64,
    pub hits: u64,
    pub misses: u64,
    pub bypasses: u64,
    pub responses: u64,
    pub refill_count: u64,
    pub refill_transactions: u64,
    pub refill_words: u64,
    pub lower_requests: u64,
    pub lower_responses: u64,
    pub hit_rate: f64,
    pub miss_rate: f64,
}

#[derive(Debug, Clone, Serialize)]
pub struct TimingMetrics {
    pub calibrated: bool,
    pub penalty_source: Option<String>,
    pub cache_miss_tmt_cycles: Option<u64>,
    pub bypass_wait_cycles: Option<u64>,
    pub total_nonhit_wait_cycles: Option<u64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TraceStats {
    pub decoded_requests: u64,
    pub strict_pc_alignment: bool,
}

#[derive(Debug, Clone)]
pub struct SimulationStats {
    pub exact: ExactMetrics,
    pub miss_3c: Miss3C,
    pub per_region: BTreeMap<String, RegionStats>,
    pub trace: TraceStats,
    pub section_stats: BTreeMap<String, RegionStats>,
}

#[derive(Debug, Clone)]
pub struct StatsCollector {
    cache_config: CacheConfig,
    exact: ExactMetrics,
    miss_3c: Miss3C,
    decoded_requests: u64,
    strict_pc_alignment: bool,
    per_region: HashMap<String, RegionAccumulator>,
    section_stats: HashMap<String, RegionAccumulator>,
}

impl StatsCollector {
    pub fn new(cache_config: CacheConfig, strict_pc_alignment: bool) -> Self {
        Self {
            cache_config,
            exact: ExactMetrics {
                requests: 0,
                hits: 0,
                misses: 0,
                bypasses: 0,
                responses: 0,
                refill_count: 0,
                refill_transactions: 0,
                refill_words: 0,
                lower_requests: 0,
                lower_responses: 0,
                hit_rate: 0.0,
                miss_rate: 0.0,
            },
            miss_3c: Miss3C::default(),
            decoded_requests: 0,
            strict_pc_alignment,
            per_region: HashMap::new(),
            section_stats: HashMap::new(),
        }
    }

    pub fn note_request(
        &mut self,
        pc: u32,
        classification: &ClassifiedAddress,
        block_number: Option<u64>,
        section_name: Option<&str>,
        access_kind: CacheAccessKind,
        miss_3c: Option<crate::cache::MissKind3C>,
        refill_words: u64,
    ) {
        self.decoded_requests += 1;
        self.exact.requests += 1;
        self.exact.responses += 1;

        match access_kind {
            CacheAccessKind::Hit => self.exact.hits += 1,
            CacheAccessKind::Miss => {
                self.exact.misses += 1;
                self.exact.refill_count += 1;
                self.exact.refill_transactions += 1;
                self.exact.refill_words += refill_words;
                self.exact.lower_requests += refill_words;
                self.exact.lower_responses += refill_words;
            }
            CacheAccessKind::Bypass => {
                self.exact.bypasses += 1;
                self.exact.lower_requests += 1;
                self.exact.lower_responses += 1;
            }
        }

        debug!(
            "stats updated pc=0x{pc:08x} region={} kind={:?} requests={} hits={} misses={} bypasses={} lower_requests={}",
            classification.region_name,
            access_kind,
            self.exact.requests,
            self.exact.hits,
            self.exact.misses,
            self.exact.bypasses,
            self.exact.lower_requests,
        );

        let region = self
            .per_region
            .entry(classification.region_name.clone())
            .or_default();
        region.requests += 1;
        region.unique_pcs.insert(pc);
        if let Some(block) = block_number {
            region.unique_blocks.insert(block);
        }

        match access_kind {
            CacheAccessKind::Hit => region.hits += 1,
            CacheAccessKind::Miss => region.misses += 1,
            CacheAccessKind::Bypass => region.bypasses += 1,
        }

        if let Some(section_name) = section_name {
            let section = self.section_stats.entry(section_name.to_string()).or_default();
            section.requests += 1;
            section.unique_pcs.insert(pc);
            if let Some(block) = block_number {
                section.unique_blocks.insert(block);
            }
            match access_kind {
                CacheAccessKind::Hit => section.hits += 1,
                CacheAccessKind::Miss => section.misses += 1,
                CacheAccessKind::Bypass => section.bypasses += 1,
            }
        }

        if let Some(miss_3c) = miss_3c {
            let slot = match miss_3c {
                crate::cache::MissKind3C::Compulsory => &mut self.miss_3c.compulsory,
                crate::cache::MissKind3C::Capacity => &mut self.miss_3c.capacity,
                crate::cache::MissKind3C::Conflict => &mut self.miss_3c.conflict,
            };
            *slot += 1;

            let region_slot = match miss_3c {
                crate::cache::MissKind3C::Compulsory => &mut region.miss_3c.compulsory,
                crate::cache::MissKind3C::Capacity => &mut region.miss_3c.capacity,
                crate::cache::MissKind3C::Conflict => &mut region.miss_3c.conflict,
            };
            *region_slot += 1;

            if let Some(section_name) = section_name {
                let section = self
                    .section_stats
                    .get_mut(section_name)
                    .expect("section stats entry must exist before miss_3c attribution");
                let section_slot = match miss_3c {
                    crate::cache::MissKind3C::Compulsory => &mut section.miss_3c.compulsory,
                    crate::cache::MissKind3C::Capacity => &mut section.miss_3c.capacity,
                    crate::cache::MissKind3C::Conflict => &mut section.miss_3c.conflict,
                };
                *section_slot += 1;
            }
        }
    }

    pub fn finish(mut self) -> Result<SimulationStats> {
        if self.exact.requests != 0 {
            self.exact.hit_rate = self.exact.hits as f64 / self.exact.requests as f64;
            self.exact.miss_rate = self.exact.misses as f64 / self.exact.requests as f64;
        }

        debug!(
            "finalizing stats requests={} hits={} misses={} bypasses={} hit_rate={:.6} miss_rate={:.6}",
            self.exact.requests,
            self.exact.hits,
            self.exact.misses,
            self.exact.bypasses,
            self.exact.hit_rate,
            self.exact.miss_rate,
        );

        self.validate()?;

        let per_region = self
            .per_region
            .into_iter()
            .map(|(name, stats)| (name, stats.finalize()))
            .collect::<BTreeMap<_, _>>();
        let section_stats = self
            .section_stats
            .into_iter()
            .map(|(name, stats)| (name, stats.finalize()))
            .collect::<BTreeMap<_, _>>();

        Ok(SimulationStats {
            exact: self.exact,
            miss_3c: self.miss_3c,
            per_region,
            trace: TraceStats {
                decoded_requests: self.decoded_requests,
                strict_pc_alignment: self.strict_pc_alignment,
            },
            section_stats,
        })
    }

    fn validate(&self) -> Result<()> {
        if self.exact.requests != self.exact.hits + self.exact.misses + self.exact.bypasses {
            bail!("invariant failed: requests != hits + misses + bypasses");
        }
        if self.exact.responses != self.exact.requests {
            bail!("invariant failed: responses != requests");
        }
        if self.miss_3c.compulsory + self.miss_3c.capacity + self.miss_3c.conflict != self.exact.misses {
            bail!("invariant failed: compulsory + capacity + conflict != misses");
        }
        let words_per_line = self.cache_config.words_per_line() as u64;
        if self.exact.refill_transactions != self.exact.misses {
            bail!("invariant failed: refill_transactions != misses");
        }
        if self.exact.refill_count != self.exact.misses {
            bail!("invariant failed: refill_count != misses");
        }
        if self.exact.refill_words != self.exact.misses * words_per_line {
            bail!("invariant failed: refill_words != misses * words_per_line");
        }
        if self.exact.lower_requests != self.exact.bypasses + self.exact.refill_words {
            bail!("invariant failed: lower_requests != bypasses + refill_words");
        }
        if self.exact.lower_responses != self.exact.lower_requests {
            bail!("invariant failed: lower_responses != lower_requests");
        }
        Ok(())
    }

    pub fn decoded_requests(&self) -> u64 {
        self.decoded_requests
    }

    pub fn hits(&self) -> u64 {
        self.exact.hits
    }

    pub fn misses(&self) -> u64 {
        self.exact.misses
    }

    pub fn bypasses(&self) -> u64 {
        self.exact.bypasses
    }
}
