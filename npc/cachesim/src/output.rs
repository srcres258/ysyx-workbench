use std::collections::BTreeMap;
use std::fmt::Write as _;

use serde::Serialize;

use crate::cache::{CacheConfig, RefillMode};
use crate::image::{ArtifactIdentity, ElfInfo};
use crate::machine::MachineProfile;
use crate::replacement::ReplacementPolicy;
use crate::stats::{
    ExactMetrics, Invariants, LocalityMetrics, Miss3C, Miss3CMetrics, RateMetrics, RegionStats,
    TimingMetrics, TrafficMetrics,
};
use crate::trace::{TraceCompression, TraceEncoding, TraceHeader};

const SCHEMA_VERSION: u32 = 2;

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
    pub config_id: String,
    pub block_bytes: u32,
    pub words_per_line: u32,
    pub total_lines: u32,
    pub ways: u32,
    pub sets: u32,
    pub capacity_bytes: u32,
    pub replacement: ReplacementPolicy,
    pub refill_mode: RefillMode,
    pub lower_transactions_per_full_refill: u32,
}

#[derive(Debug, Clone, Serialize)]
pub struct CachesimReport {
    pub schema_version: u32,
    pub cachesim_version: &'static str,
    pub input: InputSection,
    pub trace: TraceSection,
    pub machine: MachineProfile,
    pub cache: CacheSection,
    pub reference_status: ReferenceStatus,
    pub exact: ExactMetrics,
    pub rates: RateMetrics,
    pub traffic: TrafficMetrics,
    pub locality: LocalityMetrics,
    pub miss_3c: Miss3C,
    pub miss_3c_metrics: Miss3CMetrics,
    pub per_region: BTreeMap<String, RegionStats>,
    pub per_section: BTreeMap<String, RegionStats>,
    pub timing: TimingMetrics,
    pub invariants: Invariants,
    pub assumptions: Vec<String>,
    pub discrepancies: Vec<String>,
    pub elf_analysis: Option<ElfInfo>,
}

impl CachesimReport {
    pub fn summary_text(&self) -> String {
        format!(
            "CacheSim {}\ntrace={} requests={} hits={} misses={} bypasses={} cacheable_hit_rate={} miss_share_all={}\ncache={}\nreference_status={:?}",
            self.cachesim_version,
            self.input.trace.path.display(),
            self.exact.requests,
            self.exact.hits,
            self.exact.misses,
            self.exact.bypasses,
            fmt_pct(self.rates.cacheable_hit_rate),
            fmt_pct(self.rates.miss_share_all_requests),
            self.cache.config_id,
            self.reference_status,
        )
    }

    pub fn summary_report_text(&self) -> String {
        let mut out = String::new();

        writeln!(out, "{}", "=".repeat(80)).unwrap();
        writeln!(out, " NPC CACHE SUMMARY").unwrap();
        writeln!(out, " CacheSim version: {}", self.cachesim_version).unwrap();
        writeln!(out, " Schema version:   v{}", self.schema_version).unwrap();
        writeln!(out, " Reference status: {}", self.reference_status.label()).unwrap();
        writeln!(out, "{}", "=".repeat(80)).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Inputs ---").unwrap();
        writeln!(out, "  Trace:                 {}", self.input.trace.path.display()).unwrap();
        writeln!(out, "  Trace SHA-256:         {}", short_hash(&self.input.trace.sha256)).unwrap();
        writeln!(out, "  ELF:                   {}", display_artifact_path(self.input.elf.as_ref())).unwrap();
        writeln!(out, "  BIN:                   {}", display_artifact_path(self.input.bin.as_ref())).unwrap();
        writeln!(out, "  Machine:               {}", self.machine.name).unwrap();
        writeln!(out, "  Trace encoding:        {:?}", self.trace.encoding).unwrap();
        writeln!(out, "  Compression:           {:?}", self.trace.compression).unwrap();
        writeln!(out, "  Decoded requests:      {}", self.trace.decoded_requests).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Cache Geometry ---").unwrap();
        writeln!(out, "  Config ID:             {}", self.cache.config_id).unwrap();
        writeln!(out, "  Block bytes:           {}", self.cache.block_bytes).unwrap();
        writeln!(out, "  Words per line:        {}", self.cache.words_per_line).unwrap();
        writeln!(out, "  Total lines:           {}", self.cache.total_lines).unwrap();
        writeln!(out, "  Ways / Sets:           {} / {}", self.cache.ways, self.cache.sets).unwrap();
        writeln!(out, "  Capacity bytes:        {}", self.cache.capacity_bytes).unwrap();
        writeln!(out, "  Replacement:           {:?}", self.cache.replacement).unwrap();
        writeln!(out, "  Refill mode:           {}", refill_mode_label(self.cache.refill_mode)).unwrap();
        writeln!(out, "  Lower tx / refill:     {}", self.cache.lower_transactions_per_full_refill).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Request Behavior ---").unwrap();
        writeln!(out, "  Requests:              {}", self.exact.requests).unwrap();
        writeln!(out, "  Cacheable requests:    {}", self.exact.cacheable_requests).unwrap();
        writeln!(out, "  Hits:                  {}", self.exact.hits).unwrap();
        writeln!(out, "  Misses:                {}", self.exact.misses).unwrap();
        writeln!(out, "  Bypasses:              {}", self.exact.bypasses).unwrap();
        writeln!(out, "  Hit share of all req:  {}", fmt_pct(self.rates.hit_share_all_requests)).unwrap();
        writeln!(out, "  Miss share of all req: {}", fmt_pct(self.rates.miss_share_all_requests)).unwrap();
        writeln!(out, "  Bypass share of all:   {}", fmt_pct(self.rates.bypass_share_all_requests)).unwrap();
        writeln!(out, "  Cacheable hit rate:    {}", fmt_pct(self.rates.cacheable_hit_rate)).unwrap();
        writeln!(out, "  Cacheable miss rate:   {}", fmt_pct(self.rates.cacheable_miss_rate)).unwrap();
        writeln!(out, "  Misses / 1k fetches:   {}", fmt_float(self.rates.misses_per_1k_requests)).unwrap();
        writeln!(out, "  Misses / 1k cacheable: {}", fmt_float(self.rates.misses_per_1k_cacheable_requests)).unwrap();
        writeln!(out, "  Bypasses / 1k fetches: {}", fmt_float(self.rates.bypasses_per_1k_requests)).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Working Set & Locality ---").unwrap();
        writeln!(out, "  Unique dynamic PCs:     {}", self.exact.unique_dynamic_pcs).unwrap();
        writeln!(out, "  Unique cacheable PCs:   {}", self.exact.unique_cacheable_pcs).unwrap();
        writeln!(out, "  Unique bypass PCs:      {}", self.exact.unique_bypass_pcs).unwrap();
        writeln!(out, "  Unique cacheable blocks:{}", self.exact.unique_cacheable_blocks).unwrap();
        writeln!(out, "  Observed block footprint: {} B", self.exact.cacheable_block_footprint_bytes).unwrap();
        writeln!(out, "  Cache capacity / footprint: {}", fmt_ratio(self.locality.capacity_to_observed_footprint_ratio)).unwrap();
        writeln!(out, "  Unique blocks / 1k req:   {}", fmt_float(self.rates.unique_cacheable_blocks_per_1k_requests)).unwrap();
        writeln!(out, "  Avg unique words / fill:  {}", fmt_float(self.traffic.avg_unique_words_used_per_fill)).unwrap();
        writeln!(out, "  Refill word utilization:  {}", fmt_pct(self.traffic.refill_word_utilization)).unwrap();
        writeln!(out, "  Note: utilization is trace-window dependent for resident lines at end-of-run.").unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Lower-Memory Traffic ---").unwrap();
        writeln!(out, "  Refill transactions:    {}", self.exact.refill_transactions).unwrap();
        writeln!(out, "  Refill words:           {}", self.exact.refill_words).unwrap();
        writeln!(out, "  Refill bytes:           {}", self.exact.refill_bytes).unwrap();
        writeln!(out, "  Bypass bytes:           {}", self.exact.bypass_bytes).unwrap();
        writeln!(out, "  Lower requests:         {}", self.exact.lower_requests).unwrap();
        writeln!(out, "  Lower fetch bytes:      {}", self.exact.lower_fetch_bytes).unwrap();
        writeln!(out, "  Lower req / request:    {}", fmt_ratio(self.rates.lower_requests_per_request)).unwrap();
        writeln!(out, "  Lower bytes / request:  {}", fmt_ratio(self.rates.lower_bytes_per_request)).unwrap();
        writeln!(out, "  Refill bytes / cacheable req: {}", fmt_ratio(self.rates.refill_bytes_per_cacheable_request)).unwrap();
        writeln!(out, "  Traffic amplification:  {}", fmt_ratio(self.traffic.lower_traffic_amplification)).unwrap();
        writeln!(out, "  Refill amplification:   {}", fmt_ratio(self.traffic.refill_traffic_amplification)).unwrap();
        writeln!(out, "  Useful refill bytes:    {}", self.exact.useful_refill_bytes).unwrap();
        writeln!(out, "  Unused refill bytes:    {}", self.exact.unused_refill_bytes).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "--- Miss Composition ---").unwrap();
        writeln!(out, "  Compulsory:            {} ({})", self.miss_3c.compulsory, fmt_pct(self.miss_3c_metrics.compulsory_share_of_misses)).unwrap();
        writeln!(out, "  Capacity:              {} ({})", self.miss_3c.capacity, fmt_pct(self.miss_3c_metrics.capacity_share_of_misses)).unwrap();
        writeln!(out, "  Conflict:              {} ({})", self.miss_3c.conflict, fmt_pct(self.miss_3c_metrics.conflict_share_of_misses)).unwrap();
        writeln!(out, "  Compulsory / 1k cacheable: {}", fmt_float(self.miss_3c_metrics.compulsory_per_1k_cacheable_requests)).unwrap();
        writeln!(out, "  Capacity / 1k cacheable:   {}", fmt_float(self.miss_3c_metrics.capacity_per_1k_cacheable_requests)).unwrap();
        writeln!(out, "  Conflict / 1k cacheable:   {}", fmt_float(self.miss_3c_metrics.conflict_per_1k_cacheable_requests)).unwrap();
        writeln!(out, "  Note: 3C miss counts are block-size dependent.").unwrap();
        writeln!(out).unwrap();

        write_region_table(&mut out, "Region Pressure", &self.per_region);
        write_region_table(&mut out, "ELF Attribution", &self.per_section);
        write_timing_section(&mut out, &self.timing);
        write_machine_notes(&mut out, &self.machine.notes);
        write_string_list(&mut out, "Assumptions", &self.assumptions);
        write_string_list(&mut out, "Known discrepancies", &self.discrepancies);

        writeln!(out, "--- Invariants ---").unwrap();
        writeln!(out, "  Request partition:      {}", self.invariants.request_partition_ok).unwrap();
        writeln!(out, "  Response accounting:    {}", self.invariants.response_accounting_ok).unwrap();
        writeln!(out, "  Refill accounting:      {}", self.invariants.refill_accounting_ok).unwrap();
        writeln!(out, "  Lower transport acc.:   {}", self.invariants.lower_transport_accounting_ok).unwrap();
        writeln!(out, "  Lower fetch bytes acc.: {}", self.invariants.lower_fetch_bytes_ok).unwrap();
        writeln!(out, "  3C partition:           {}", self.invariants.miss_3c_partition_ok).unwrap();
        writeln!(out).unwrap();

        writeln!(out, "{}", "=".repeat(80)).unwrap();
        writeln!(out, " End of cachesim summary.").unwrap();
        writeln!(out, "{}", "=".repeat(80)).unwrap();

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

fn short_hash(hash: &str) -> &str {
    hash.get(..12).unwrap_or(hash)
}

fn refill_mode_label(mode: RefillMode) -> &'static str {
    match mode {
        RefillMode::IndependentWord => "independent-word",
    }
}

fn fmt_pct(value: Option<f64>) -> String {
    value.map(|v| format!("{:.2}%", v * 100.0)).unwrap_or_else(|| "N/A".to_string())
}

fn fmt_float(value: Option<f64>) -> String {
    value.map(|v| format!("{:.2}", v)).unwrap_or_else(|| "N/A".to_string())
}

fn fmt_ratio(value: Option<f64>) -> String {
    value.map(|v| format!("{:.4}", v)).unwrap_or_else(|| "N/A".to_string())
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
        let key_a = (stats_a.lower_fetch_bytes, stats_a.misses, stats_a.requests, stats_a.unique_blocks);
        let key_b = (stats_b.lower_fetch_bytes, stats_b.misses, stats_b.requests, stats_b.unique_blocks);
        key_b.cmp(&key_a).then_with(|| name_a.cmp(name_b))
    });

    writeln!(out, "  {:<18} {:>9} {:>9} {:>9} {:>9} {:>12} {:>10}", "Name", "Req", "Miss", "Bypass", "Hit%", "LowerBytes", "MissShr").unwrap();
    writeln!(out, "  {}", "-".repeat(86)).unwrap();
    for (name, row) in rows.into_iter().take(8) {
        writeln!(
            out,
            "  {:<18} {:>9} {:>9} {:>9} {:>9} {:>12} {:>10}",
            name,
            row.requests,
            row.misses,
            row.bypasses,
            fmt_pct(row.cacheable_hit_rate),
            row.lower_fetch_bytes,
            fmt_pct(row.share_of_total_misses),
        )
        .unwrap();
    }
    writeln!(out).unwrap();
}

fn write_timing_section(out: &mut String, timing: &TimingMetrics) {
    writeln!(out, "--- Timing / TMT ---").unwrap();
    writeln!(out, "  Calibrated:              {}", timing.calibrated).unwrap();
    writeln!(out, "  Penalty source:          {}", timing.penalty_source.as_deref().unwrap_or("N/A")).unwrap();
    if timing.calibrated {
        writeln!(out, "  Cache miss wait cycles:  {}", timing.cache_miss_tmt_cycles.unwrap_or(0)).unwrap();
        writeln!(out, "  Bypass wait cycles:      {}", timing.bypass_wait_cycles.unwrap_or(0)).unwrap();
        writeln!(out, "  Total non-hit cycles:    {}", timing.total_nonhit_wait_cycles.unwrap_or(0)).unwrap();
        writeln!(out, "  Miss cycles / request:   {}", fmt_ratio(timing.cache_miss_tmt_cycles_per_request)).unwrap();
        writeln!(out, "  Non-hit cycles / request:{}", fmt_ratio(timing.total_nonhit_wait_cycles_per_request)).unwrap();
        writeln!(out, "  Miss cycles / 1k req:    {}", fmt_float(timing.cache_miss_tmt_cycles_per_1k_requests)).unwrap();
        writeln!(out, "  Non-hit cycles / 1k req: {}", fmt_float(timing.total_nonhit_wait_cycles_per_1k_requests)).unwrap();
        writeln!(out, "  Miss cycles / cacheable req: {}", fmt_ratio(timing.cache_miss_tmt_cycles_per_cacheable_request)).unwrap();
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
        schema_version: SCHEMA_VERSION,
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
            config_id: format!(
                "b{}-l{}-w{}-{}-{}-{}",
                cache.block_bytes,
                cache.total_lines,
                cache.ways,
                replacement_label(cache.replacement),
                refill_mode_label(RefillMode::IndependentWord),
                "ysyxsoc-current"
            ),
            block_bytes: cache.block_bytes,
            words_per_line: cache.words_per_line(),
            total_lines: cache.total_lines,
            ways: cache.ways,
            sets: cache.sets(),
            capacity_bytes: cache.capacity_bytes(),
            replacement: cache.replacement,
            refill_mode: RefillMode::IndependentWord,
            lower_transactions_per_full_refill: cache.words_per_line(),
        },
        reference_status,
        exact: stats.exact,
        rates: stats.rates,
        traffic: stats.traffic,
        locality: stats.locality,
        miss_3c: stats.miss_3c,
        miss_3c_metrics: stats.miss_3c_metrics,
        per_region: stats.per_region,
        per_section: stats.section_stats,
        timing,
        invariants: stats.invariants,
        assumptions,
        discrepancies,
        elf_analysis: elf_info,
    }
}

fn replacement_label(policy: ReplacementPolicy) -> &'static str {
    match policy {
        ReplacementPolicy::Lru => "lru",
        ReplacementPolicy::Fifo => "fifo",
    }
}
