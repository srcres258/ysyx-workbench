use std::path::PathBuf;

use anyhow::{Context, Result, bail};
use clap::{Parser, Subcommand};

use cachesim::cache::{CacheConfig, CacheModel};
use cachesim::compare::compare_reports;
use cachesim::image::{hash_file, load_elf_info};
use cachesim::machine::MachineProfile;
use cachesim::output::{ReferenceStatus, build_report};
use cachesim::replacement::ReplacementPolicy;
use cachesim::stats::{StatsCollector, TimingMetrics};
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
    let machine = MachineProfile::builtin(&args.machine)?;
    let cache_config = CacheConfig {
        block_bytes: args.block_bytes,
        total_lines: args.total_lines,
        ways: args.ways,
        replacement: args.replacement,
    }
    .validate()?;
    let mut cache = CacheModel::new(cache_config);
    let trace_identity = hash_file(&args.trace)?;
    let elf_info = args.elf.as_deref().map(load_elf_info).transpose()?;
    let elf_identity = elf_info.as_ref().map(|info| info.identity.clone());
    let bin_identity = args.bin.as_deref().map(hash_file).transpose()?;
    let timing_model = args.timing_config.as_deref().map(TimingModel::from_path).transpose()?;

    let mut trace = TraceReader::open(&args.trace)?;
    let mut stats = StatsCollector::new(cache_config, args.strict_pc_alignment);

    while let Some(record) = trace.next_record()? {
        match record {
            TraceRecord::SinglePc(pc) => {
                simulate_pc(
                    pc,
                    &machine,
                    elf_info.as_ref(),
                    args.strict_pc_alignment,
                    &mut cache,
                    &mut stats,
                )?;
            }
            TraceRecord::Run { start_pc, count } => {
                for i in 0..count {
                    let pc = start_pc.wrapping_add(i.wrapping_mul(PCTR_V1_RUN_STRIDE));
                    simulate_pc(
                        pc,
                        &machine,
                        elf_info.as_ref(),
                        args.strict_pc_alignment,
                        &mut cache,
                        &mut stats,
                    )?;
                }
            }
        }
    }

    let stats = stats.finish()?;
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
        std::fs::write(&output, &json)
            .with_context(|| format!("failed to write report: {}", output.display()))?;
    }
    println!("{}\n", report.summary_text());
    println!("{json}");
    Ok(())
}

fn simulate_pc(
    pc: u32,
    machine: &MachineProfile,
    elf_info: Option<&cachesim::image::ElfInfo>,
    strict_pc_alignment: bool,
    cache: &mut CacheModel,
    stats: &mut StatsCollector,
) -> Result<()> {
    if strict_pc_alignment && (pc & 0b11) != 0 {
        bail!("misaligned PC in strict mode: 0x{pc:08x}");
    }
    let classification = machine.classify(pc);
    let section_name = elf_info.and_then(|elf| elf.find_section(pc));
    let access = cache.access(pc, classification.instruction_cacheable);
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
        return TimingMetrics {
            calibrated: false,
            penalty_source: None,
            cache_miss_tmt_cycles: None,
            bypass_wait_cycles: None,
            total_nonhit_wait_cycles: None,
        };
    };

    let mut miss_cycles = 0_u64;
    let mut bypass_cycles = 0_u64;
    let mut calibrated = true;
    for (region, region_stats) in &stats.per_region {
        if region_stats.misses != 0 {
            match model.lookup_miss(region, cache_config.block_bytes, refill_mode) {
                Some(cycles) => miss_cycles += cycles * region_stats.misses,
                None => calibrated = false,
            }
        }
        if region_stats.bypasses != 0 {
            match model.lookup_bypass(region) {
                Some(cycles) => bypass_cycles += cycles * region_stats.bypasses,
                None => calibrated = false,
            }
        }
    }

    if !calibrated {
        return TimingMetrics {
            calibrated: false,
            penalty_source: Some(model.source().to_string()),
            cache_miss_tmt_cycles: None,
            bypass_wait_cycles: None,
            total_nonhit_wait_cycles: None,
        };
    }

    TimingMetrics {
        calibrated: true,
        penalty_source: Some(model.source().to_string()),
        cache_miss_tmt_cycles: Some(miss_cycles),
        bypass_wait_cycles: Some(bypass_cycles),
        total_nonhit_wait_cycles: Some(miss_cycles + bypass_cycles),
    }
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Commands::Simulate(args) => simulate(args),
        Commands::Compare(args) => {
            let message = compare_reports(&args.cachesim_json, &args.perf_json)?;
            println!("{message}");
            Ok(())
        }
    }
}
