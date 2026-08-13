use std::collections::BTreeMap;

use serde::Serialize;

use crate::cache::{CacheConfig, RefillMode};
use crate::image::{ArtifactIdentity, ElfInfo};
use crate::machine::MachineProfile;
use crate::replacement::ReplacementPolicy;
use crate::stats::{Miss3C, RegionStats, TimingMetrics};
use crate::trace::{TraceCompression, TraceEncoding, TraceHeader};

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ReferenceStatus {
    RtlExact,
    Prospective,
}

#[derive(Debug, Clone, Serialize)]
pub struct InputSection {
    pub trace: ArtifactIdentity,
    pub elf: Option<ArtifactIdentity>,
    pub bin: Option<ArtifactIdentity>,
}

#[derive(Debug, Clone, Serialize)]
pub struct TraceSection {
    pub pctr_version: u16,
    pub encoding: TraceEncoding,
    pub compression: TraceCompression,
    pub address_width: u8,
    pub decoded_requests: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct CacheSection {
    pub block_bytes: u32,
    pub total_lines: u32,
    pub ways: u32,
    pub sets: u32,
    pub capacity_bytes: u32,
    pub replacement: ReplacementPolicy,
    pub refill_mode: RefillMode,
}

#[derive(Debug, Clone, Serialize)]
pub struct CachesimReport {
    pub schema_version: &'static str,
    pub cachesim_version: &'static str,
    pub input: InputSection,
    pub trace: TraceSection,
    pub machine: MachineProfile,
    pub cache: CacheSection,
    pub reference_status: ReferenceStatus,
    pub exact: crate::stats::ExactMetrics,
    pub miss_3c: Miss3C,
    pub per_region: BTreeMap<String, RegionStats>,
    pub per_section: BTreeMap<String, RegionStats>,
    pub timing: TimingMetrics,
    pub assumptions: Vec<String>,
    pub discrepancies: Vec<String>,
    pub elf_analysis: Option<ElfInfo>,
}

impl CachesimReport {
    pub fn summary_text(&self) -> String {
        format!(
            "CacheSim {}\ntrace={} requests={} hits={} misses={} bypasses={} hit_rate={:.4} miss_rate={:.4}\ncache={}B blocks, {} lines, {} ways, refill={}\nreference_status={:?}",
            self.cachesim_version,
            self.input.trace.path.display(),
            self.exact.requests,
            self.exact.hits,
            self.exact.misses,
            self.exact.bypasses,
            self.exact.hit_rate,
            self.exact.miss_rate,
            self.cache.block_bytes,
            self.cache.total_lines,
            self.cache.ways,
            match self.cache.refill_mode {
                RefillMode::IndependentWord => "independent-word",
            },
            self.reference_status,
        )
    }
}

pub fn build_report(
    trace_identity: ArtifactIdentity,
    elf_identity: Option<ArtifactIdentity>,
    bin_identity: Option<ArtifactIdentity>,
    elf_info: Option<ElfInfo>,
    trace_header: &TraceHeader,
    compression: TraceCompression,
    machine: MachineProfile,
    cache: CacheConfig,
    reference_status: ReferenceStatus,
    stats: crate::stats::SimulationStats,
    timing: TimingMetrics,
    assumptions: Vec<String>,
    discrepancies: Vec<String>,
) -> CachesimReport {
    CachesimReport {
        schema_version: "cachesim-report-v1",
        cachesim_version: env!("CARGO_PKG_VERSION"),
        input: InputSection {
            trace: trace_identity,
            elf: elf_identity,
            bin: bin_identity,
        },
        trace: TraceSection {
            pctr_version: trace_header.version,
            encoding: trace_header.encoding,
            compression,
            address_width: trace_header.address_width,
            decoded_requests: stats.trace.decoded_requests,
        },
        machine,
        cache: CacheSection {
            block_bytes: cache.block_bytes,
            total_lines: cache.total_lines,
            ways: cache.ways,
            sets: cache.sets(),
            capacity_bytes: cache.capacity_bytes(),
            replacement: cache.replacement,
            refill_mode: RefillMode::IndependentWord,
        },
        reference_status,
        exact: stats.exact,
        miss_3c: stats.miss_3c,
        per_region: stats.per_region,
        per_section: stats.section_stats,
        timing,
        assumptions,
        discrepancies,
        elf_analysis: elf_info,
    }
}
