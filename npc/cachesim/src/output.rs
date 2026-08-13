use std::collections::BTreeMap;
use std::fmt::Write as _;

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

    pub fn summary_report_text(&self) -> String {
        let mut out = String::new();
        let cacheable_requests = self.exact.hits + self.exact.misses;
        let nonhit_requests = self.exact.misses + self.exact.bypasses;
        let avg_refill_words = ratio(self.exact.refill_words, self.exact.misses);
        let lower_requests_per_request = ratio(self.exact.lower_requests, self.exact.requests);
        let miss_per_kinst = per_1k(self.exact.misses, self.exact.requests);
        let bypass_per_kinst = per_1k(self.exact.bypasses, self.exact.requests);

        writeln!(out, "{}", "=".repeat(72)).unwrap();
        writeln!(out, " NPC CACHE SUMMARY").unwrap();
        writeln!(out, " CacheSim version: {}", self.cachesim_version).unwrap();
        writeln!(out, " Reference status: {}", self.reference_status.label()).unwrap();
        writeln!(out, "{}", "=".repeat(72)).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Inputs ---").unwrap();
        writeln!(out, "  Trace:               {}", self.input.trace.path.display()).unwrap();
        writeln!(out, "  ELF:                 {}", display_artifact_path(self.input.elf.as_ref())).unwrap();
        writeln!(out, "  BIN:                 {}", display_artifact_path(self.input.bin.as_ref())).unwrap();
        writeln!(out, "  Machine:             {}", self.machine.name).unwrap();
        writeln!(out, "  Trace encoding:      {:?}", self.trace.encoding).unwrap();
        writeln!(out, "  Compression:         {:?}", self.trace.compression).unwrap();
        writeln!(out, "  Decoded requests:    {}", self.trace.decoded_requests).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Cache Geometry ---").unwrap();
        writeln!(out, "  Block bytes:         {}", self.cache.block_bytes).unwrap();
        writeln!(out, "  Total lines:         {}", self.cache.total_lines).unwrap();
        writeln!(out, "  Ways / Sets:         {} / {}", self.cache.ways, self.cache.sets).unwrap();
        writeln!(out, "  Capacity bytes:      {}", self.cache.capacity_bytes).unwrap();
        writeln!(out, "  Replacement:         {:?}", self.cache.replacement).unwrap();
        writeln!(out, "  Refill mode:         {}", refill_mode_label(self.cache.refill_mode)).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Exact Request Accounting ---").unwrap();
        writeln!(out, "  Requests:            {}", self.exact.requests).unwrap();
        writeln!(out, "  Hits:                {}", self.exact.hits).unwrap();
        writeln!(out, "  Misses:              {}", self.exact.misses).unwrap();
        writeln!(out, "  Bypasses:            {}", self.exact.bypasses).unwrap();
        writeln!(out, "  Hit rate:            {:.2}%", self.exact.hit_rate * 100.0).unwrap();
        writeln!(out, "  Miss rate:           {:.2}%", self.exact.miss_rate * 100.0).unwrap();
        writeln!(out, "  Bypass rate:         {:.2}%", percent(self.exact.bypasses, self.exact.requests)).unwrap();
        writeln!(out, "  Lower requests:      {}", self.exact.lower_requests).unwrap();
        writeln!(out, "  Refill transactions: {}", self.exact.refill_transactions).unwrap();
        writeln!(out, "  Refill words:        {}", self.exact.refill_words).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- I-cache DSE Signals ---").unwrap();
        writeln!(out, "  Cacheable requests:      {}", cacheable_requests).unwrap();
        writeln!(out, "  Cacheable hit rate:      {:.2}%", percent(self.exact.hits, cacheable_requests)).unwrap();
        writeln!(out, "  Non-hit share:           {:.2}%", percent(nonhit_requests, self.exact.requests)).unwrap();
        writeln!(out, "  Misses / 1k requests:    {:.2}", miss_per_kinst).unwrap();
        writeln!(out, "  Bypasses / 1k requests:  {:.2}", bypass_per_kinst).unwrap();
        writeln!(out, "  Avg refill words/miss:   {:.2}", avg_refill_words).unwrap();
        writeln!(out, "  Lower req / request:     {:.4}", lower_requests_per_request).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- 3C Miss Breakdown ---").unwrap();
        writeln!(out, "  Compulsory:         {} ({:.2}%)", self.miss_3c.compulsory, percent(self.miss_3c.compulsory, self.exact.misses)).unwrap();
        writeln!(out, "  Capacity:           {} ({:.2}%)", self.miss_3c.capacity, percent(self.miss_3c.capacity, self.exact.misses)).unwrap();
        writeln!(out, "  Conflict:           {} ({:.2}%)", self.miss_3c.conflict, percent(self.miss_3c.conflict, self.exact.misses)).unwrap();
        writeln!(out).unwrap();

        write_region_table(&mut out, "Top regions by non-hit pressure", &self.per_region);
        write_region_table(&mut out, "Top ELF sections by non-hit pressure", &self.per_section);
        write_timing_section(&mut out, &self.timing, self.exact.requests, self.exact.misses, self.exact.bypasses);
        write_machine_notes(&mut out, &self.machine.notes);
        write_string_list(&mut out, "Assumptions", &self.assumptions);
        write_string_list(&mut out, "Known discrepancies", &self.discrepancies);

        writeln!(out, "{}", "=".repeat(72)).unwrap();
        writeln!(out, " End of cachesim summary.").unwrap();
        writeln!(out, "{}", "=".repeat(72)).unwrap();

        out
    }
}

impl ReferenceStatus {
    fn label(&self) -> &'static str {
        match self {
            Self::RtlExact => "rtl_exact",
            Self::Prospective => "prospective",
        }
    }
}

fn display_artifact_path(artifact: Option<&ArtifactIdentity>) -> String {
    artifact
        .map(|artifact| artifact.path.display().to_string())
        .unwrap_or_else(|| "N/A".to_string())
}

fn refill_mode_label(mode: RefillMode) -> &'static str {
    match mode {
        RefillMode::IndependentWord => "independent-word",
    }
}

fn ratio(num: u64, den: u64) -> f64 {
    if den == 0 {
        0.0
    } else {
        num as f64 / den as f64
    }
}

fn percent(num: u64, den: u64) -> f64 {
    ratio(num, den) * 100.0
}

fn per_1k(num: u64, den: u64) -> f64 {
    ratio(num, den) * 1000.0
}

fn write_region_table(out: &mut String, title: &str, stats: &BTreeMap<String, RegionStats>) {
    writeln!(out, "--- {title} ---").unwrap();
    if stats.is_empty() {
        writeln!(out, "  (no data available)").unwrap();
        writeln!(out).unwrap();
        return;
    }

    let mut rows = stats.iter().collect::<Vec<_>>();
    rows.sort_by(|(name_a, stats_a), (name_b, stats_b)| {
        let key_a = (
            stats_a.misses + stats_a.bypasses,
            stats_a.misses,
            stats_a.requests,
            stats_a.unique_cache_blocks,
        );
        let key_b = (
            stats_b.misses + stats_b.bypasses,
            stats_b.misses,
            stats_b.requests,
            stats_b.unique_cache_blocks,
        );
        key_b.cmp(&key_a).then_with(|| name_a.cmp(name_b))
    });

    writeln!(out, "  {:<18} {:>10} {:>10} {:>10} {:>9} {:>9}", "Name", "Req", "Miss", "Bypass", "Hit%", "Blocks").unwrap();
    writeln!(out, "  {}", "-".repeat(70)).unwrap();
    for (name, row) in rows.into_iter().take(8) {
        writeln!(
            out,
            "  {:<18} {:>10} {:>10} {:>10} {:>8.2} {:>9}",
            name,
            row.requests,
            row.misses,
            row.bypasses,
            percent(row.hits, row.requests),
            row.unique_cache_blocks,
        )
        .unwrap();
    }
    writeln!(out).unwrap();
}

fn write_timing_section(
    out: &mut String,
    timing: &TimingMetrics,
    total_requests: u64,
    total_misses: u64,
    total_bypasses: u64,
) {
    writeln!(out, "--- Timing-derived non-hit impact ---").unwrap();
    writeln!(out, "  Calibrated:          {}", timing.calibrated).unwrap();
    writeln!(
        out,
        "  Penalty source:      {}",
        timing.penalty_source.as_deref().unwrap_or("N/A")
    )
    .unwrap();

    if timing.calibrated {
        let miss_wait = timing.cache_miss_tmt_cycles.unwrap_or(0);
        let bypass_wait = timing.bypass_wait_cycles.unwrap_or(0);
        let total_wait = timing.total_nonhit_wait_cycles.unwrap_or(0);
        writeln!(out, "  Miss wait cycles:    {}", miss_wait).unwrap();
        writeln!(out, "  Bypass wait cycles:  {}", bypass_wait).unwrap();
        writeln!(out, "  Total wait cycles:   {}", total_wait).unwrap();
        writeln!(out, "  Wait / request:      {:.4}", ratio(total_wait, total_requests)).unwrap();
        writeln!(out, "  Wait / miss:         {:.4}", ratio(miss_wait, total_misses)).unwrap();
        writeln!(out, "  Wait / bypass:       {:.4}", ratio(bypass_wait, total_bypasses)).unwrap();
    } else {
        writeln!(out, "  Timing penalties unavailable for this run.").unwrap();
    }
    writeln!(out).unwrap();
}

fn write_machine_notes(out: &mut String, notes: &[&str]) {
    writeln!(out, "--- Machine policy notes ---").unwrap();
    if notes.is_empty() {
        writeln!(out, "  (none)").unwrap();
    } else {
        for note in notes {
            writeln!(out, "  - {note}").unwrap();
        }
    }
    writeln!(out).unwrap();
}

fn write_string_list(out: &mut String, title: &str, items: &[String]) {
    writeln!(out, "--- {title} ---").unwrap();
    if items.is_empty() {
        writeln!(out, "  (none)").unwrap();
    } else {
        for item in items {
            writeln!(out, "  - {item}").unwrap();
        }
    }
    writeln!(out).unwrap();
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
