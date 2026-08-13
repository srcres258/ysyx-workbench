use std::collections::HashMap;
use std::path::Path;

use anyhow::{Context, Result, bail};
use log::{debug, info};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
struct PerfCounterEntry {
    name: String,
    value: u64,
}

#[derive(Debug, Deserialize)]
struct PerfJson {
    perf_counters: Vec<PerfCounterEntry>,
}

pub fn compare_reports(cachesim_json: &Path, perf_json: &Path) -> Result<String> {
    info!(
        "comparing cachesim report {} against perf report {}",
        cachesim_json.display(),
        perf_json.display(),
    );
    let cachesim_text = std::fs::read_to_string(cachesim_json)
        .with_context(|| format!("failed to read cachesim JSON: {}", cachesim_json.display()))?;
    let perf_text = std::fs::read_to_string(perf_json)
        .with_context(|| format!("failed to read perf JSON: {}", perf_json.display()))?;
    let cachesim: serde_json::Value = serde_json::from_str(&cachesim_text)
        .with_context(|| format!("failed to parse cachesim JSON: {}", cachesim_json.display()))?;
    let perf = serde_json::from_str::<PerfJson>(&perf_text)
        .with_context(|| format!("failed to parse perf JSON: {}", perf_json.display()))?;

    let perf_map = perf
        .perf_counters
        .into_iter()
        .map(|entry| (entry.name, entry.value))
        .collect::<HashMap<_, _>>();

    let exact = cachesim
        .get("exact")
        .ok_or_else(|| anyhow::anyhow!("cachesim JSON missing 'exact' object"))?;
    let get_exact = |name: &str| -> Result<u64> {
        exact
            .get(name)
            .and_then(|value| value.as_u64())
            .ok_or_else(|| anyhow::anyhow!("cachesim JSON missing exact.{name}"))
    };

    let checks = [
        ("icache.request.count", get_exact("requests")?),
        ("icache.hit.count", get_exact("hits")?),
        ("icache.miss.count", get_exact("misses")?),
        ("icache.bypass.count", get_exact("bypasses")?),
        ("icache.refill.count", get_exact("refill_count")?),
        ("icache.refill_transaction.count", get_exact("refill_transactions")?),
        ("icache.refill_word.count", get_exact("refill_words")?),
        ("icache.lower_req.count", get_exact("lower_requests")?),
        ("icache.lower_resp.count", get_exact("lower_responses")?),
        ("icache.response.count", get_exact("responses")?),
    ];

    for (name, expected) in checks {
        let actual = perf_map
            .get(name)
            .copied()
            .ok_or_else(|| anyhow::anyhow!("perf counter missing: {name}"))?;
        debug!("compare counter {name}: cachesim={} perf={}", expected, actual);
        if actual != expected {
            bail!("counter mismatch: {name}: cachesim={expected}, perf={actual}");
        }
    }

    info!("all structural I-cache counters matched between cachesim and perf reports");
    Ok("All structural I-cache counters match.".to_string())
}
