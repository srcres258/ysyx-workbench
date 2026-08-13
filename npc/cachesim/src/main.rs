use std::path::PathBuf;
use std::time::Instant;

use anyhow::{Context, Result, bail};
use clap::{Parser, Subcommand};
use log::{debug, error, info, warn};

use cachesim::cache::{CacheConfig, CacheModel};
use cachesim::compare::compare_reports;
use cachesim::image::{hash_file, load_elf_info};
use cachesim::machine::MachineProfile;
use cachesim::output::{ReferenceStatus, build_report};
use cachesim::replacement::ReplacementPolicy;
use cachesim::stats::{StatsCollector, TimingMetrics, TimingRegionMetrics};
use cachesim::timing::TimingModel;
use cachesim::trace::{PCTR_V1_RUN_STRIDE, TraceReader, TraceRecord};

#[derive(Parser)]
#[command(version, about = "Architectural I-cache reference simulator for NPC")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Simulate(SimulateArgs),
    Compare(CompareArgs),
}

#[derive(clap::Args)]
struct SimulateArgs {
    #[arg(long)]
    trace: PathBuf,
    #[arg(long)]
    elf: Option<PathBuf>,
    #[arg(long)]
    bin: Option<PathBuf>,
    #[arg(long, default_value = "ysyxsoc-current")]
    machine: String,
    #[arg(long, default_value_t = 4)]
    block_bytes: u32,
    #[arg(long = "lines", default_value_t = 8)]
    total_lines: u32,
    #[arg(long, default_value_t = 1)]
    ways: u32,
    #[arg(long, value_enum, default_value_t = ReplacementPolicy::Lru)]
    replacement: ReplacementPolicy,
    #[arg(long)]
    output: Option<PathBuf>,
    #[arg(long = "output-txt")]
    output_txt: Option<PathBuf>,
    #[arg(long)]
    timing_config: Option<PathBuf>,
    #[arg(long, default_value_t = true)]
    strict_pc_alignment: bool,
}

#[derive(clap::Args)]
struct CompareArgs {
    #[arg(long)]
    cachesim_json: PathBuf,
    #[arg(long)]
    perf_json: PathBuf,
}

fn simulate(args: SimulateArgs) -> Result<()> {
    info!(
        "starting cachesim simulate trace={} machine={} block_bytes={} total_lines={} ways={} replacement={:?} strict_pc_alignment={} elf={} bin={} timing_config={}",
        args.trace.display(),
        args.machine,
        args.block_bytes,
        args.total_lines,
        args.ways,
        args.replacement,
        args.strict_pc_alignment,
        args.elf
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "none".to_string()),
        args.bin
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "none".to_string()),
        args.timing_config
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_else(|| "none".to_string()),
    );
    let mut machine = MachineProfile::builtin(&args.machine)?;
    info!(
        "using machine profile {} with {} regions",
        machine.name,
        machine.regions.len(),
    );
    let cache_config = CacheConfig {
        block_bytes: args.block_bytes,
        total_lines: args.total_lines,
        ways: args.ways,
        replacement: args.replacement,
    }
    .validate()?;
    info!(
        "validated cache geometry: block_bytes={} total_lines={} ways={} sets={} capacity_bytes={} replacement={:?}",
        cache_config.block_bytes,
        cache_config.total_lines,
        cache_config.ways,
        cache_config.sets(),
        cache_config.capacity_bytes(),
        cache_config.replacement,
    );
    let mut cache = CacheModel::new(cache_config);
    let trace_identity = hash_file(&args.trace)?;
    let elf_info = match args.elf.as_deref() {
        Some(path) => Some(load_elf_info(path)?),
        None => {
            warn!("no ELF provided; section-level attribution will be unavailable");
            None
        }
    };
    let elf_identity = elf_info.as_ref().map(|info| info.identity.clone());
    let bin_identity = match args.bin.as_deref() {
        Some(path) => Some(hash_file(path)?),
        None => {
            warn!("no BIN provided; binary artifact identity will be omitted from the report");
            None
        }
    };
    let timing_model = match args.timing_config.as_deref() {
        Some(path) => Some(TimingModel::from_path(path)?),
        None => {
            warn!("no timing calibration provided; timing-derived miss impact will remain uncalibrated");
            None
        }
    };

    let mut trace = TraceReader::open(&args.trace)?;
    let mut stats = StatsCollector::new(cache_config, args.strict_pc_alignment);
    info!(
        "beginning trace replay for {} (encoding={:?}, compression={:?})",
        trace.path().display(),
        trace.header().encoding,
        trace.compression(),
    );
    let mut last_progress_report = Instant::now();
    let mut logical_requests = 0_u64;
    let mut next_progress_request = 100_000_u64;
    let mut warned_misaligned_pcs = std::collections::BTreeSet::new();

    while let Some(record) = trace.next_record()? {
        match record {
            TraceRecord::SinglePc(pc) => {
                logical_requests += 1;
                simulate_pc(
                    pc,
                    &mut machine,
                    elf_info.as_ref(),
                    args.strict_pc_alignment,
                    &mut cache,
                    &mut stats,
                    &mut warned_misaligned_pcs,
                )?;
                report_progress(&stats, logical_requests, &mut next_progress_request, &mut last_progress_report);
            }
            TraceRecord::Run { start_pc, count } => {
                debug!(
                    "expanding run record start_pc=0x{start_pc:08x} count={} stride={}",
                    count,
                    PCTR_V1_RUN_STRIDE,
                );
                for i in 0..count {
                    logical_requests += 1;
                    let pc = start_pc.wrapping_add(i.wrapping_mul(PCTR_V1_RUN_STRIDE));
                    simulate_pc(
                        pc,
                        &mut machine,
                        elf_info.as_ref(),
                        args.strict_pc_alignment,
                        &mut cache,
                        &mut stats,
                        &mut warned_misaligned_pcs,
                    )?;
                    report_progress(&stats, logical_requests, &mut next_progress_request, &mut last_progress_report);
                }
            }
        }
    }

    if !warned_misaligned_pcs.is_empty() {
        let examples = warned_misaligned_pcs
            .iter()
            .take(8)
            .map(|pc| format!("0x{pc:08x}"))
            .collect::<Vec<_>>()
            .join(", ");
        warn!(
            "observed {} distinct misaligned PCs in non-strict mode; examples: {}",
            warned_misaligned_pcs.len(),
            examples,
        );
    }

    let line_utilization = cache.finalize_line_utilization();
    stats.set_line_utilization(line_utilization);
    let stats = stats.finish()?;
    info!(
        "finished trace replay: requests={} hits={} misses={} bypasses={} hit_rate={:.6} miss_rate={:.6}",
        stats.exact.requests,
        stats.exact.hits,
        stats.exact.misses,
        stats.exact.bypasses,
        stats.exact.hit_rate,
        stats.exact.miss_rate,
    );
    let completed_request_count = stats.exact.requests;
    let timing = build_timing_metrics(&stats, cache_config, cache.refill_mode(), timing_model.as_ref());
    let report = build_report(
        trace_identity,
        elf_identity,
        bin_identity,
        elf_info,
        trace.header(),
        trace.compression(),
        machine,
        cache_config,
        if cache_config.ways == 1 {
            ReferenceStatus::RtlExact
        } else {
            ReferenceStatus::Prospective
        },
        stats,
        timing,
        vec![
            "Current cachesim v1 assumes all lower-memory instruction-side transactions succeed.".to_string(),
            "Current cachesim v1 assumes no behaviorally relevant self-modifying code or fence.i invalidation during the measured window.".to_string(),
            "Current architectural PCTR-to-I-cache request equivalence is valid only for the current non-speculative blocking frontend.".to_string(),
        ],
        vec![
            "Current repo perf_aggregator.py still carries a stale lower_req<=miss+bypass strict check for multi-word lines.".to_string(),
            "Current repo docs still contain stale 4B×16 default-geometry wording; RTL default is 4B×8.".to_string(),
        ],
    );

    let json = serde_json::to_string_pretty(&report)?;
    if let Some(output) = args.output {
        write_output_file(&output, &json, "JSON report")?;
    }
    if let Some(output_txt) = args.output_txt {
        let summary = report.summary_report_text();
        write_output_file(&output_txt, &summary, "text summary")?;
    }
    info!(
        "completed cachesim simulate for trace={} requests={}",
        trace.path().display(),
        completed_request_count,
    );
    println!("{}\n", report.summary_text());
    println!("{json}");
    Ok(())
}

fn report_progress(
    stats: &StatsCollector,
    logical_requests: u64,
    next_progress_request: &mut u64,
    last_progress_report: &mut Instant,
) {
    let reached_request_checkpoint = logical_requests >= *next_progress_request;
    let reached_time_checkpoint = logical_requests % 1_000 == 0 && last_progress_report.elapsed().as_secs() >= 1;
    if !reached_request_checkpoint && !reached_time_checkpoint {
        return;
    }

    info!(
        "trace replay progress: requests={} hits={} misses={} bypasses={}",
        logical_requests,
        stats.hits(),
        stats.misses(),
        stats.bypasses(),
    );

    if reached_request_checkpoint {
        while logical_requests >= *next_progress_request {
            *next_progress_request += 100_000;
        }
    }
    *last_progress_report = Instant::now();
}

fn write_output_file(path: &PathBuf, contents: &str, label: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent).with_context(|| {
                format!("failed to create parent directory for {label}: {}", path.display())
            })?;
        }
    }
    std::fs::write(path, contents)
        .with_context(|| format!("failed to write {label}: {}", path.display()))?;
    info!(
        "wrote {} to {} ({} bytes)",
        label,
        path.display(),
        contents.len(),
    );
    Ok(())
}

fn simulate_pc(
    pc: u32,
    machine: &mut MachineProfile,
    elf_info: Option<&cachesim::image::ElfInfo>,
    strict_pc_alignment: bool,
    cache: &mut CacheModel,
    stats: &mut StatsCollector,
    warned_misaligned_pcs: &mut std::collections::BTreeSet<u32>,
) -> Result<()> {
    if strict_pc_alignment && (pc & 0b11) != 0 {
        bail!("misaligned PC in strict mode: 0x{pc:08x}");
    }
    if !strict_pc_alignment && (pc & 0b11) != 0 {
        warned_misaligned_pcs.insert(pc);
    }
    let classification = machine.classify(pc);
    let section_name = elf_info.and_then(|elf| elf.find_section(pc));
    let access = cache.access(pc, classification.instruction_cacheable);
    debug!(
        "simulate_pc pc=0x{pc:08x} region={} cacheable={} recognized_region={} section={} access={:?} block={:?} miss_3c={:?} refill_words={}",
        classification.region_name,
        classification.instruction_cacheable,
        classification.recognized_region,
        section_name.unwrap_or("<none>"),
        access.kind,
        access.block_number,
        access.miss_3c,
        access.refill_words,
    );
    stats.note_request(
        pc,
        &classification,
        access.block_number,
        section_name,
        access.kind,
        access.miss_3c,
        access.refill_words,
    );
    Ok(())
}

fn build_timing_metrics(
    stats: &cachesim::stats::SimulationStats,
    cache_config: CacheConfig,
    refill_mode: cachesim::cache::RefillMode,
    timing_model: Option<&TimingModel>,
) -> TimingMetrics {
    let Some(model) = timing_model else {
        warn!("timing model unavailable; returning uncalibrated timing metrics");
        return TimingMetrics {
            calibrated: false,
            penalty_source: None,
            cache_miss_tmt_cycles: None,
            bypass_wait_cycles: None,
            total_nonhit_wait_cycles: None,
            cache_miss_tmt_cycles_per_request: None,
            total_nonhit_wait_cycles_per_request: None,
            cache_miss_tmt_cycles_per_1k_requests: None,
            total_nonhit_wait_cycles_per_1k_requests: None,
            cache_miss_tmt_cycles_per_cacheable_request: None,
            per_region: std::collections::BTreeMap::new(),
        };
    };

    let mut miss_cycles = 0_u64;
    let mut bypass_cycles = 0_u64;
    let mut calibrated = true;
    let mut per_region = std::collections::BTreeMap::new();
    for (region, region_stats) in &stats.per_region {
        let mut region_miss_cycles = 0_u64;
        let mut region_bypass_cycles = 0_u64;
        if region_stats.misses != 0 {
            match model.lookup_miss(region, cache_config.block_bytes, refill_mode) {
                Some(cycles) => {
                    region_miss_cycles = cycles * region_stats.misses;
                    miss_cycles += region_miss_cycles;
                }
                None => {
                    warn!(
                        "missing timing calibration for miss penalties: region={} block_bytes={} refill_mode={:?} misses={}",
                        region,
                        cache_config.block_bytes,
                        refill_mode,
                        region_stats.misses,
                    );
                    calibrated = false;
                }
            }
        }
        if region_stats.bypasses != 0 {
            match model.lookup_bypass(region) {
                Some(cycles) => {
                    region_bypass_cycles = cycles * region_stats.bypasses;
                    bypass_cycles += region_bypass_cycles;
                }
                None => {
                    warn!(
                        "missing timing calibration for bypass penalties: region={} bypasses={}",
                        region,
                        region_stats.bypasses,
                    );
                    calibrated = false;
                }
            }
        }

        per_region.insert(
            region.clone(),
            TimingRegionMetrics {
                estimated_miss_wait_cycles: Some(region_miss_cycles),
                estimated_bypass_wait_cycles: Some(region_bypass_cycles),
                estimated_total_nonhit_cycles: Some(region_miss_cycles + region_bypass_cycles),
                share_of_total_nonhit_cycles: None,
            },
        );
    }

    if !calibrated {
        warn!(
            "timing calibration incomplete for source {}; report will omit derived timing totals",
            model.source(),
        );
        return TimingMetrics {
            calibrated: false,
            penalty_source: Some(model.source().to_string()),
            cache_miss_tmt_cycles: None,
            bypass_wait_cycles: None,
            total_nonhit_wait_cycles: None,
            cache_miss_tmt_cycles_per_request: None,
            total_nonhit_wait_cycles_per_request: None,
            cache_miss_tmt_cycles_per_1k_requests: None,
            total_nonhit_wait_cycles_per_1k_requests: None,
            cache_miss_tmt_cycles_per_cacheable_request: None,
            per_region: std::collections::BTreeMap::new(),
        };
    }

    let total_wait = miss_cycles + bypass_cycles;
    for region_metrics in per_region.values_mut() {
        region_metrics.share_of_total_nonhit_cycles = region_metrics
            .estimated_total_nonhit_cycles
            .and_then(|cycles| if total_wait == 0 { None } else { Some(cycles as f64 / total_wait as f64) });
    }

    info!(
        "timing calibration applied from {}: miss_cycles={} bypass_cycles={} total_nonhit_wait_cycles={}",
        model.source(),
        miss_cycles,
        bypass_cycles,
        total_wait,
    );

    TimingMetrics {
        calibrated: true,
        penalty_source: Some(model.source().to_string()),
        cache_miss_tmt_cycles: Some(miss_cycles),
        bypass_wait_cycles: Some(bypass_cycles),
        total_nonhit_wait_cycles: Some(total_wait),
        cache_miss_tmt_cycles_per_request: if stats.exact.requests == 0 { None } else { Some(miss_cycles as f64 / stats.exact.requests as f64) },
        total_nonhit_wait_cycles_per_request: if stats.exact.requests == 0 { None } else { Some(total_wait as f64 / stats.exact.requests as f64) },
        cache_miss_tmt_cycles_per_1k_requests: if stats.exact.requests == 0 { None } else { Some(miss_cycles as f64 * 1000.0 / stats.exact.requests as f64) },
        total_nonhit_wait_cycles_per_1k_requests: if stats.exact.requests == 0 { None } else { Some(total_wait as f64 * 1000.0 / stats.exact.requests as f64) },
        cache_miss_tmt_cycles_per_cacheable_request: if stats.exact.cacheable_requests == 0 { None } else { Some(miss_cycles as f64 / stats.exact.cacheable_requests as f64) },
        per_region,
    }
}

fn main() -> Result<()> {
    let _ = env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .try_init();

    let cli = Cli::parse();
    let result = match cli.command {
        Commands::Simulate(args) => simulate(args),
        Commands::Compare(args) => match compare_reports(&args.cachesim_json, &args.perf_json) {
            Ok(message) => {
                println!("{message}");
                Ok(())
            }
            Err(err) => Err(err),
        },
    };

    if let Err(err) = &result {
        error!("cachesim failed: {err:#}");
    }

    result
}
