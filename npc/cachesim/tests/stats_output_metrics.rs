use cachesim::cache::{CacheConfig, CacheModel};
use cachesim::image::ArtifactIdentity;
use cachesim::machine::MachineProfile;
use cachesim::output::{ReferenceStatus, build_report};
use cachesim::replacement::ReplacementPolicy;
use cachesim::stats::{StatsCollector, TimingMetrics};
use cachesim::trace::{PCTR_ENDIANNESS_LITTLE, TraceCompression, TraceEncoding, TraceHeader};

fn make_header() -> TraceHeader {
    TraceHeader {
        version: 1,
        header_size: 16,
        address_width: 4,
        encoding: TraceEncoding::Raw,
        endianness: PCTR_ENDIANNESS_LITTLE,
    }
}

fn dummy_artifact(path: &str) -> ArtifactIdentity {
    ArtifactIdentity {
        path: path.into(),
        sha256: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".to_string(),
        size_bytes: 16,
    }
}

fn empty_timing() -> TimingMetrics {
    TimingMetrics {
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
        per_region: Default::default(),
    }
}

fn approx_eq(actual: Option<f64>, expected: f64) {
    let actual = actual.expect("metric should be present");
    assert!((actual - expected).abs() < 1e-9, "actual={actual} expected={expected}");
}

#[test]
fn derived_metrics_and_report_stay_consistent() {
    let cache_config = CacheConfig {
        block_bytes: 16,
        total_lines: 2,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    }
    .validate()
    .unwrap();

    let mut cache = CacheModel::new(cache_config);
    let mut machine = MachineProfile::builtin("ysyxsoc-current").unwrap();
    let mut stats = StatsCollector::new(cache_config, true);

    for pc in [0x3000_0000_u32, 0x3000_0000, 0x3000_0004, 0x2000_0000] {
        let classification = machine.classify(pc);
        let access = cache.access(pc, classification.instruction_cacheable);
        stats.note_request(
            pc,
            &classification,
            access.block_number,
            None,
            access.kind,
            access.miss_3c,
            access.refill_words,
        );
    }

    stats.set_line_utilization(cache.finalize_line_utilization());
    let simulation = stats.finish().unwrap();

    assert_eq!(simulation.exact.requests, 4);
    assert_eq!(simulation.exact.cacheable_requests, 3);
    assert_eq!(simulation.exact.hits, 2);
    assert_eq!(simulation.exact.misses, 1);
    assert_eq!(simulation.exact.bypasses, 1);
    assert_eq!(simulation.exact.refill_words, 4);
    assert_eq!(simulation.exact.refill_bytes, 16);
    assert_eq!(simulation.exact.bypass_bytes, 4);
    assert_eq!(simulation.exact.lower_fetch_bytes, 20);
    assert_eq!(simulation.exact.instruction_demand_bytes, 16);
    assert_eq!(simulation.exact.cacheable_instruction_demand_bytes, 12);
    assert_eq!(simulation.exact.unique_cacheable_blocks, 1);
    assert_eq!(simulation.exact.cacheable_block_footprint_bytes, 16);
    assert_eq!(simulation.exact.refill_words_used, 2);
    assert_eq!(simulation.exact.unused_refill_words, 2);
    assert_eq!(simulation.exact.unused_refill_bytes, 8);

    approx_eq(simulation.rates.cacheable_hit_rate, 2.0 / 3.0);
    approx_eq(simulation.rates.cacheable_miss_rate, 1.0 / 3.0);
    approx_eq(simulation.rates.misses_per_1k_requests, 250.0);
    approx_eq(simulation.rates.misses_per_1k_cacheable_requests, 1000.0 / 3.0);
    approx_eq(simulation.rates.bypasses_per_1k_requests, 250.0);
    approx_eq(simulation.rates.lower_bytes_per_request, 5.0);
    approx_eq(simulation.rates.lower_bytes_per_cacheable_request, 16.0 / 3.0);
    approx_eq(simulation.rates.refill_bytes_per_cacheable_request, 16.0 / 3.0);
    approx_eq(simulation.rates.unique_cacheable_blocks_per_1k_requests, 250.0);

    approx_eq(simulation.traffic.lower_traffic_amplification, 20.0 / 16.0);
    approx_eq(simulation.traffic.refill_traffic_amplification, 16.0 / 12.0);
    approx_eq(simulation.traffic.refill_word_utilization, 0.5);
    approx_eq(simulation.traffic.refill_overfetch_ratio, 0.5);
    approx_eq(simulation.traffic.avg_unique_words_used_per_fill, 2.0);
    approx_eq(simulation.traffic.avg_unique_bytes_used_per_fill, 8.0);

    approx_eq(simulation.locality.capacity_to_observed_footprint_ratio, 2.0);
    approx_eq(simulation.miss_3c_metrics.compulsory_share_of_misses, 1.0);
    approx_eq(simulation.miss_3c_metrics.compulsory_per_1k_cacheable_requests, 1000.0 / 3.0);

    let flash = simulation.per_region.get("flash").unwrap();
    assert_eq!(flash.requests, 3);
    assert_eq!(flash.cacheable_requests, 3);
    assert_eq!(flash.unique_blocks, 1);
    assert_eq!(flash.lower_fetch_bytes, 16);
    approx_eq(flash.cacheable_hit_rate, 2.0 / 3.0);

    let mrom = simulation.per_region.get("mrom").unwrap();
    assert_eq!(mrom.requests, 1);
    assert_eq!(mrom.cacheable_requests, 0);
    assert_eq!(mrom.bypasses, 1);
    assert_eq!(mrom.lower_fetch_bytes, 4);
    assert_eq!(mrom.cacheable_hit_rate, None);

    let report = build_report(
        dummy_artifact("trace.pctrace"),
        None,
        None,
        None,
        &make_header(),
        TraceCompression::None,
        MachineProfile::builtin("ysyxsoc-current").unwrap(),
        cache_config,
        ReferenceStatus::RtlExact,
        simulation.clone(),
        empty_timing(),
        vec![],
        vec![],
    );

    assert_eq!(report.schema_version, 2);
    assert_eq!(report.cache.config_id, "b16-l2-w1-lru-independent-word-ysyxsoc-current");
    assert_eq!(report.rates.cacheable_hit_rate, simulation.rates.cacheable_hit_rate);
    let summary = report.summary_report_text();
    assert!(summary.contains("Cacheable hit rate:    66.67%"));
    assert!(summary.contains("Refill word utilization:  50.00%"));
    assert!(summary.contains("Traffic amplification:  1.2500"));
}

#[test]
fn line_utilization_finalizes_on_eviction_and_trace_end() {
    let config = CacheConfig {
        block_bytes: 16,
        total_lines: 1,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    };
    let mut cache = CacheModel::new(config);

    cache.access(0x3000_0000, true);
    cache.access(0x3000_0000, true);
    cache.access(0x3000_0010, true);

    let utilization = cache.finalize_line_utilization();
    assert_eq!(utilization.line_fill_count, 2);
    assert_eq!(utilization.refill_words_fetched, 8);
    assert_eq!(utilization.refill_words_used, 2);
}
