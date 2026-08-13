use std::collections::{BTreeMap, HashMap, HashSet};

use anyhow::{Result, bail};
use log::debug;
use serde::Serialize;

use crate::cache::{CacheAccessKind, CacheConfig, LineUtilizationStats};
use crate::machine::ClassifiedAddress;

const INSTRUCTION_BYTES: u64 = 4;

#[derive(Debug, Clone, Copy, Default, Serialize)]
pub struct Miss3C {
    pub compulsory: u64,
    pub capacity: u64,
    pub conflict: u64,
}

#[derive(Debug, Clone, Copy, Default, Serialize)]
pub struct Invariants {
    pub request_partition_ok: bool,
    pub response_accounting_ok: bool,
    pub refill_accounting_ok: bool,
    pub lower_transport_accounting_ok: bool,
    pub lower_fetch_bytes_ok: bool,
    pub miss_3c_partition_ok: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct ExactMetrics {
    pub requests: u64,
    pub cacheable_requests: u64,
    pub hits: u64,
    pub misses: u64,
    pub bypasses: u64,
    pub responses: u64,
    pub refill_count: u64,
    pub refill_transactions: u64,
    pub refill_words: u64,
    pub refill_bytes: u64,
    pub bypass_bytes: u64,
    pub lower_requests: u64,
    pub lower_responses: u64,
    pub lower_fetch_bytes: u64,
    pub instruction_demand_bytes: u64,
    pub cacheable_instruction_demand_bytes: u64,
    pub unique_dynamic_pcs: u64,
    pub unique_cacheable_pcs: u64,
    pub unique_bypass_pcs: u64,
    pub unique_cacheable_blocks: u64,
    pub cacheable_block_footprint_bytes: u64,
    pub line_fill_count: u64,
    pub refill_words_fetched: u64,
    pub refill_words_used: u64,
    pub useful_refill_bytes: u64,
    pub unused_refill_words: u64,
    pub unused_refill_bytes: u64,
    pub hit_rate: f64,
    pub miss_rate: f64,
}

#[derive(Debug, Clone, Serialize)]
pub struct RateMetrics {
    pub hit_share_all_requests: Option<f64>,
    pub miss_share_all_requests: Option<f64>,
    pub bypass_share_all_requests: Option<f64>,
    pub cacheable_hit_rate: Option<f64>,
    pub cacheable_miss_rate: Option<f64>,
    pub misses_per_1k_requests: Option<f64>,
    pub misses_per_1k_cacheable_requests: Option<f64>,
    pub bypasses_per_1k_requests: Option<f64>,
    pub lower_requests_per_request: Option<f64>,
    pub lower_bytes_per_request: Option<f64>,
    pub lower_bytes_per_cacheable_request: Option<f64>,
    pub refill_bytes_per_cacheable_request: Option<f64>,
    pub unique_cacheable_blocks_per_1k_requests: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TrafficMetrics {
    pub lower_traffic_amplification: Option<f64>,
    pub refill_traffic_amplification: Option<f64>,
    pub refill_word_utilization: Option<f64>,
    pub useful_refill_ratio: Option<f64>,
    pub refill_overfetch_ratio: Option<f64>,
    pub avg_unique_words_used_per_fill: Option<f64>,
    pub avg_unique_bytes_used_per_fill: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct LocalityMetrics {
    pub cache_capacity_bytes: u64,
    pub capacity_to_observed_footprint_ratio: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Miss3CMetrics {
    pub compulsory_share_of_misses: Option<f64>,
    pub capacity_share_of_misses: Option<f64>,
    pub conflict_share_of_misses: Option<f64>,
    pub compulsory_per_1k_cacheable_requests: Option<f64>,
    pub capacity_per_1k_cacheable_requests: Option<f64>,
    pub conflict_per_1k_cacheable_requests: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TimingMetrics {
    pub calibrated: bool,
    pub penalty_source: Option<String>,
    pub cache_miss_tmt_cycles: Option<u64>,
    pub bypass_wait_cycles: Option<u64>,
    pub total_nonhit_wait_cycles: Option<u64>,
    pub cache_miss_tmt_cycles_per_request: Option<f64>,
    pub total_nonhit_wait_cycles_per_request: Option<f64>,
    pub cache_miss_tmt_cycles_per_1k_requests: Option<f64>,
    pub total_nonhit_wait_cycles_per_1k_requests: Option<f64>,
    pub cache_miss_tmt_cycles_per_cacheable_request: Option<f64>,
    pub per_region: BTreeMap<String, TimingRegionMetrics>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TimingRegionMetrics {
    pub estimated_miss_wait_cycles: Option<u64>,
    pub estimated_bypass_wait_cycles: Option<u64>,
    pub estimated_total_nonhit_cycles: Option<u64>,
    pub share_of_total_nonhit_cycles: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TraceStats {
    pub decoded_requests: u64,
    pub strict_pc_alignment: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct RegionStats {
    pub requests: u64,
    pub cacheable_requests: u64,
    pub hits: u64,
    pub misses: u64,
    pub bypasses: u64,
    pub unique_pcs: u64,
    pub unique_blocks: u64,
    pub refill_transactions: u64,
    pub refill_words: u64,
    pub refill_bytes: u64,
    pub bypass_bytes: u64,
    pub lower_requests: u64,
    pub lower_fetch_bytes: u64,
    pub miss_3c: Miss3C,
    pub cacheable_hit_rate: Option<f64>,
    pub cacheable_miss_rate: Option<f64>,
    pub misses_per_1k_requests: Option<f64>,
    pub misses_per_1k_cacheable_requests: Option<f64>,
    pub lower_bytes_per_region_request: Option<f64>,
    pub region_share_of_lower_requests: Option<f64>,
    pub region_share_of_lower_fetch_bytes: Option<f64>,
    pub region_share_of_misses: Option<f64>,
    pub share_of_total_misses: Option<f64>,
}

#[derive(Debug, Clone)]
struct RegionAccumulator {
    requests: u64,
    hits: u64,
    misses: u64,
    bypasses: u64,
    refill_transactions: u64,
    refill_words: u64,
    refill_bytes: u64,
    bypass_bytes: u64,
    lower_requests: u64,
    lower_fetch_bytes: u64,
    miss_3c: Miss3C,
    unique_pcs: HashSet<u32>,
    unique_blocks: HashSet<u64>,
}

impl Default for RegionAccumulator {
    fn default() -> Self {
        Self {
            requests: 0,
            hits: 0,
            misses: 0,
            bypasses: 0,
            refill_transactions: 0,
            refill_words: 0,
            refill_bytes: 0,
            bypass_bytes: 0,
            lower_requests: 0,
            lower_fetch_bytes: 0,
            miss_3c: Miss3C::default(),
            unique_pcs: HashSet::new(),
            unique_blocks: HashSet::new(),
        }
    }
}

impl RegionAccumulator {
    fn note_request(&mut self, pc: u32, block_number: Option<u64>, access_kind: CacheAccessKind, refill_words: u64) {
        self.requests += 1;
        self.unique_pcs.insert(pc);
        if let Some(block) = block_number {
            self.unique_blocks.insert(block);
        }

        match access_kind {
            CacheAccessKind::Hit => self.hits += 1,
            CacheAccessKind::Miss => {
                self.misses += 1;
                self.refill_transactions += 1;
                self.refill_words += refill_words;
                self.refill_bytes += refill_words * INSTRUCTION_BYTES;
                self.lower_requests += refill_words;
                self.lower_fetch_bytes += refill_words * INSTRUCTION_BYTES;
            }
            CacheAccessKind::Bypass => {
                self.bypasses += 1;
                self.bypass_bytes += INSTRUCTION_BYTES;
                self.lower_requests += 1;
                self.lower_fetch_bytes += INSTRUCTION_BYTES;
            }
        }
    }

    fn finalize(&self, global_exact: &ExactMetrics) -> RegionStats {
        let cacheable_requests = self.hits + self.misses;
        RegionStats {
            requests: self.requests,
            cacheable_requests,
            hits: self.hits,
            misses: self.misses,
            bypasses: self.bypasses,
            unique_pcs: self.unique_pcs.len() as u64,
            unique_blocks: self.unique_blocks.len() as u64,
            refill_transactions: self.refill_transactions,
            refill_words: self.refill_words,
            refill_bytes: self.refill_bytes,
            bypass_bytes: self.bypass_bytes,
            lower_requests: self.lower_requests,
            lower_fetch_bytes: self.lower_fetch_bytes,
            miss_3c: self.miss_3c,
            cacheable_hit_rate: option_ratio(self.hits, cacheable_requests),
            cacheable_miss_rate: option_ratio(self.misses, cacheable_requests),
            misses_per_1k_requests: per_1k(self.misses, self.requests),
            misses_per_1k_cacheable_requests: per_1k(self.misses, cacheable_requests),
            lower_bytes_per_region_request: option_ratio(self.lower_fetch_bytes, self.requests),
            region_share_of_lower_requests: option_ratio(self.lower_requests, global_exact.lower_requests),
            region_share_of_lower_fetch_bytes: option_ratio(self.lower_fetch_bytes, global_exact.lower_fetch_bytes),
            region_share_of_misses: option_ratio(self.misses, global_exact.misses),
            share_of_total_misses: option_ratio(self.misses, global_exact.misses),
        }
    }
}

#[derive(Debug, Clone)]
pub struct SimulationStats {
    pub exact: ExactMetrics,
    pub rates: RateMetrics,
    pub traffic: TrafficMetrics,
    pub locality: LocalityMetrics,
    pub miss_3c: Miss3C,
    pub miss_3c_metrics: Miss3CMetrics,
    pub per_region: BTreeMap<String, RegionStats>,
    pub trace: TraceStats,
    pub section_stats: BTreeMap<String, RegionStats>,
    pub invariants: Invariants,
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
    unique_dynamic_pcs: HashSet<u32>,
    unique_cacheable_pcs: HashSet<u32>,
    unique_bypass_pcs: HashSet<u32>,
    unique_cacheable_blocks: HashSet<u64>,
    line_utilization: LineUtilizationStats,
}

impl StatsCollector {
    pub fn new(cache_config: CacheConfig, strict_pc_alignment: bool) -> Self {
        Self {
            cache_config,
            exact: ExactMetrics {
                requests: 0,
                cacheable_requests: 0,
                hits: 0,
                misses: 0,
                bypasses: 0,
                responses: 0,
                refill_count: 0,
                refill_transactions: 0,
                refill_words: 0,
                refill_bytes: 0,
                bypass_bytes: 0,
                lower_requests: 0,
                lower_responses: 0,
                lower_fetch_bytes: 0,
                instruction_demand_bytes: 0,
                cacheable_instruction_demand_bytes: 0,
                unique_dynamic_pcs: 0,
                unique_cacheable_pcs: 0,
                unique_bypass_pcs: 0,
                unique_cacheable_blocks: 0,
                cacheable_block_footprint_bytes: 0,
                line_fill_count: 0,
                refill_words_fetched: 0,
                refill_words_used: 0,
                useful_refill_bytes: 0,
                unused_refill_words: 0,
                unused_refill_bytes: 0,
                hit_rate: 0.0,
                miss_rate: 0.0,
            },
            miss_3c: Miss3C::default(),
            decoded_requests: 0,
            strict_pc_alignment,
            per_region: HashMap::new(),
            section_stats: HashMap::new(),
            unique_dynamic_pcs: HashSet::new(),
            unique_cacheable_pcs: HashSet::new(),
            unique_bypass_pcs: HashSet::new(),
            unique_cacheable_blocks: HashSet::new(),
            line_utilization: LineUtilizationStats::default(),
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
        self.exact.instruction_demand_bytes += INSTRUCTION_BYTES;
        self.unique_dynamic_pcs.insert(pc);

        match access_kind {
            CacheAccessKind::Hit => {
                self.exact.cacheable_requests += 1;
                self.exact.cacheable_instruction_demand_bytes += INSTRUCTION_BYTES;
                self.exact.hits += 1;
                self.unique_cacheable_pcs.insert(pc);
            }
            CacheAccessKind::Miss => {
                self.exact.cacheable_requests += 1;
                self.exact.cacheable_instruction_demand_bytes += INSTRUCTION_BYTES;
                self.exact.misses += 1;
                self.exact.refill_count += 1;
                self.exact.refill_transactions += 1;
                self.exact.refill_words += refill_words;
                self.exact.refill_bytes += refill_words * INSTRUCTION_BYTES;
                self.exact.lower_requests += refill_words;
                self.exact.lower_responses += refill_words;
                self.exact.lower_fetch_bytes += refill_words * INSTRUCTION_BYTES;
                self.unique_cacheable_pcs.insert(pc);
                if let Some(block) = block_number {
                    self.unique_cacheable_blocks.insert(block);
                }
            }
            CacheAccessKind::Bypass => {
                self.exact.bypasses += 1;
                self.exact.bypass_bytes += INSTRUCTION_BYTES;
                self.exact.lower_requests += 1;
                self.exact.lower_responses += 1;
                self.exact.lower_fetch_bytes += INSTRUCTION_BYTES;
                self.unique_bypass_pcs.insert(pc);
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

        let region = self.per_region.entry(classification.region_name.clone()).or_default();
        region.note_request(pc, block_number, access_kind, refill_words);

        if let Some(section_name) = section_name {
            self.section_stats
                .entry(section_name.to_string())
                .or_default()
                .note_request(pc, block_number, access_kind, refill_words);
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

    pub fn set_line_utilization(&mut self, line_utilization: LineUtilizationStats) {
        self.line_utilization = line_utilization;
    }

    pub fn finish(mut self) -> Result<SimulationStats> {
        self.exact.unique_dynamic_pcs = self.unique_dynamic_pcs.len() as u64;
        self.exact.unique_cacheable_pcs = self.unique_cacheable_pcs.len() as u64;
        self.exact.unique_bypass_pcs = self.unique_bypass_pcs.len() as u64;
        self.exact.unique_cacheable_blocks = self.unique_cacheable_blocks.len() as u64;
        self.exact.cacheable_block_footprint_bytes = self.exact.unique_cacheable_blocks * self.cache_config.block_bytes as u64;
        self.exact.line_fill_count = self.line_utilization.line_fill_count;
        self.exact.refill_words_fetched = self.line_utilization.refill_words_fetched;
        self.exact.refill_words_used = self.line_utilization.refill_words_used;
        self.exact.useful_refill_bytes = self.exact.refill_words_used * INSTRUCTION_BYTES;
        self.exact.unused_refill_words = self.exact.refill_words_fetched.saturating_sub(self.exact.refill_words_used);
        self.exact.unused_refill_bytes = self.exact.unused_refill_words * INSTRUCTION_BYTES;

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

        let invariants = self.validate()?;

        let per_region = self
            .per_region
            .iter()
            .map(|(name, stats)| (name.clone(), stats.finalize(&self.exact)))
            .collect::<BTreeMap<_, _>>();
        let section_stats = self
            .section_stats
            .iter()
            .map(|(name, stats)| (name.clone(), stats.finalize(&self.exact)))
            .collect::<BTreeMap<_, _>>();

        Ok(SimulationStats {
            rates: RateMetrics::from_exact(&self.exact),
            traffic: TrafficMetrics::from_exact(&self.exact),
            locality: LocalityMetrics::from_exact(&self.exact, self.cache_config),
            miss_3c_metrics: Miss3CMetrics::from_counts(self.miss_3c, self.exact.cacheable_requests),
            exact: self.exact,
            miss_3c: self.miss_3c,
            per_region,
            trace: TraceStats {
                decoded_requests: self.decoded_requests,
                strict_pc_alignment: self.strict_pc_alignment,
            },
            section_stats,
            invariants,
        })
    }

    fn validate(&self) -> Result<Invariants> {
        let request_partition_ok = self.exact.requests == self.exact.hits + self.exact.misses + self.exact.bypasses;
        if !request_partition_ok {
            bail!("invariant failed: requests != hits + misses + bypasses");
        }

        let response_accounting_ok = self.exact.responses == self.exact.requests
            && self.exact.lower_responses == self.exact.lower_requests;
        if !response_accounting_ok {
            bail!("invariant failed: response accounting mismatch");
        }

        let miss_3c_partition_ok = self.miss_3c.compulsory + self.miss_3c.capacity + self.miss_3c.conflict == self.exact.misses;
        if !miss_3c_partition_ok {
            bail!("invariant failed: compulsory + capacity + conflict != misses");
        }

        let words_per_line = self.cache_config.words_per_line() as u64;
        let refill_accounting_ok = self.exact.refill_transactions == self.exact.misses
            && self.exact.refill_count == self.exact.misses
            && self.exact.refill_words == self.exact.misses * words_per_line
            && self.exact.refill_bytes == self.exact.refill_words * INSTRUCTION_BYTES;
        if !refill_accounting_ok {
            bail!("invariant failed: refill accounting mismatch");
        }

        let lower_transport_accounting_ok = self.exact.lower_requests == self.exact.bypasses + self.exact.refill_words
            && self.exact.bypass_bytes == self.exact.bypasses * INSTRUCTION_BYTES;
        if !lower_transport_accounting_ok {
            bail!("invariant failed: lower transport accounting mismatch");
        }

        let lower_fetch_bytes_ok = self.exact.lower_fetch_bytes == self.exact.lower_requests * INSTRUCTION_BYTES;
        if !lower_fetch_bytes_ok {
            bail!("invariant failed: lower_fetch_bytes != lower_requests * 4");
        }

        Ok(Invariants {
            request_partition_ok,
            response_accounting_ok,
            refill_accounting_ok,
            lower_transport_accounting_ok,
            lower_fetch_bytes_ok,
            miss_3c_partition_ok,
        })
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

impl RateMetrics {
    fn from_exact(exact: &ExactMetrics) -> Self {
        Self {
            hit_share_all_requests: option_ratio(exact.hits, exact.requests),
            miss_share_all_requests: option_ratio(exact.misses, exact.requests),
            bypass_share_all_requests: option_ratio(exact.bypasses, exact.requests),
            cacheable_hit_rate: option_ratio(exact.hits, exact.cacheable_requests),
            cacheable_miss_rate: option_ratio(exact.misses, exact.cacheable_requests),
            misses_per_1k_requests: per_1k(exact.misses, exact.requests),
            misses_per_1k_cacheable_requests: per_1k(exact.misses, exact.cacheable_requests),
            bypasses_per_1k_requests: per_1k(exact.bypasses, exact.requests),
            lower_requests_per_request: option_ratio(exact.lower_requests, exact.requests),
            lower_bytes_per_request: option_ratio(exact.lower_fetch_bytes, exact.requests),
            lower_bytes_per_cacheable_request: option_ratio(exact.refill_bytes, exact.cacheable_requests),
            refill_bytes_per_cacheable_request: option_ratio(exact.refill_bytes, exact.cacheable_requests),
            unique_cacheable_blocks_per_1k_requests: per_1k(exact.unique_cacheable_blocks, exact.requests),
        }
    }
}

impl TrafficMetrics {
    fn from_exact(exact: &ExactMetrics) -> Self {
        Self {
            lower_traffic_amplification: option_ratio(exact.lower_fetch_bytes, exact.instruction_demand_bytes),
            refill_traffic_amplification: option_ratio(exact.refill_bytes, exact.cacheable_instruction_demand_bytes),
            refill_word_utilization: option_ratio(exact.refill_words_used, exact.refill_words_fetched),
            useful_refill_ratio: option_ratio(exact.useful_refill_bytes, exact.refill_bytes),
            refill_overfetch_ratio: option_ratio(exact.unused_refill_bytes, exact.refill_bytes),
            avg_unique_words_used_per_fill: option_ratio(exact.refill_words_used, exact.line_fill_count),
            avg_unique_bytes_used_per_fill: option_ratio(exact.useful_refill_bytes, exact.line_fill_count),
        }
    }
}

impl LocalityMetrics {
    fn from_exact(exact: &ExactMetrics, cache_config: CacheConfig) -> Self {
        Self {
            cache_capacity_bytes: cache_config.capacity_bytes() as u64,
            capacity_to_observed_footprint_ratio: option_ratio(
                cache_config.capacity_bytes() as u64,
                exact.cacheable_block_footprint_bytes,
            ),
        }
    }
}

impl Miss3CMetrics {
    fn from_counts(miss_3c: Miss3C, cacheable_requests: u64) -> Self {
        let total_misses = miss_3c.compulsory + miss_3c.capacity + miss_3c.conflict;
        Self {
            compulsory_share_of_misses: option_ratio(miss_3c.compulsory, total_misses),
            capacity_share_of_misses: option_ratio(miss_3c.capacity, total_misses),
            conflict_share_of_misses: option_ratio(miss_3c.conflict, total_misses),
            compulsory_per_1k_cacheable_requests: per_1k(miss_3c.compulsory, cacheable_requests),
            capacity_per_1k_cacheable_requests: per_1k(miss_3c.capacity, cacheable_requests),
            conflict_per_1k_cacheable_requests: per_1k(miss_3c.conflict, cacheable_requests),
        }
    }
}

fn option_ratio(num: u64, den: u64) -> Option<f64> {
    if den == 0 {
        None
    } else {
        Some(num as f64 / den as f64)
    }
}

fn per_1k(num: u64, den: u64) -> Option<f64> {
    option_ratio(num, den).map(|value| value * 1000.0)
}
