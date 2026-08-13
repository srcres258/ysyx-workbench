use std::collections::HashMap;
use std::path::Path;

use anyhow::{Context, Result};
use log::{debug, info, warn};
use serde::{Deserialize, Serialize};

use crate::cache::RefillMode;

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct PenaltyEntry {
    pub region: String,
    pub block_bytes: u32,
    pub refill_mode: String,
    pub cycles: u64,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct BypassPenaltyEntry {
    pub region: String,
    pub cycles: u64,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct TimingCalibrationFile {
    #[serde(default)]
    pub cache_miss_penalties: Vec<PenaltyEntry>,
    #[serde(default)]
    pub bypass_penalties: Vec<BypassPenaltyEntry>,
}

#[derive(Debug, Clone)]
pub struct TimingModel {
    source: String,
    miss_penalties: HashMap<(String, u32, String), u64>,
    bypass_penalties: HashMap<String, u64>,
}

impl TimingModel {
    pub fn from_path(path: &Path) -> Result<Self> {
        info!("loading timing calibration from {}", path.display());
        let text = std::fs::read_to_string(path)
            .with_context(|| format!("failed to read timing file: {}", path.display()))?;
        let extension = path.extension().and_then(|ext| ext.to_str());
        if extension != Some("json") && extension != Some("toml") {
            warn!(
                "timing file {} has extension {:?}; attempting TOML parse by default",
                path.display(),
                extension,
            );
        }
        let parsed = if extension == Some("json") {
            serde_json::from_str::<TimingCalibrationFile>(&text)
                .with_context(|| format!("failed to parse JSON timing file: {}", path.display()))?
        } else {
            toml::from_str::<TimingCalibrationFile>(&text)
                .with_context(|| format!("failed to parse TOML timing file: {}", path.display()))?
        };

        let miss_penalties: HashMap<(String, u32, String), u64> = parsed
            .cache_miss_penalties
            .into_iter()
            .map(|entry| ((entry.region, entry.block_bytes, entry.refill_mode), entry.cycles))
            .collect();
        let bypass_penalties: HashMap<String, u64> = parsed
            .bypass_penalties
            .into_iter()
            .map(|entry| (entry.region, entry.cycles))
            .collect();

        debug!(
            "loaded timing calibration {} with {} miss penalties and {} bypass penalties",
            path.display(),
            miss_penalties.len(),
            bypass_penalties.len(),
        );

        Ok(Self {
            source: path.display().to_string(),
            miss_penalties,
            bypass_penalties,
        })
    }

    pub fn source(&self) -> &str {
        &self.source
    }

    pub fn lookup_miss(&self, region: &str, block_bytes: u32, refill_mode: RefillMode) -> Option<u64> {
        let refill_mode = match refill_mode {
            RefillMode::IndependentWord => "independent-word",
        };
        self.miss_penalties
            .get(&(region.to_string(), block_bytes, refill_mode.to_string()))
            .copied()
    }

    pub fn lookup_bypass(&self, region: &str) -> Option<u64> {
        self.bypass_penalties.get(region).copied()
    }
}
