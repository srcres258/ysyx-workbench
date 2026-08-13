use anyhow::{Result, bail};
use serde::Serialize;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RegionKind {
    Flash,
    Psram,
    Sdram,
    Sram,
    Mrom,
    Uart,
    Gpio,
    Keyboard,
    Vga,
    SpiController,
    Unknown,
}

impl RegionKind {
    pub fn display_name(self) -> &'static str {
        match self {
            Self::Flash => "flash",
            Self::Psram => "psram",
            Self::Sdram => "sdram",
            Self::Sram => "sram",
            Self::Mrom => "mrom",
            Self::Uart => "uart",
            Self::Gpio => "gpio",
            Self::Keyboard => "keyboard",
            Self::Vga => "vga",
            Self::SpiController => "spi_controller",
            Self::Unknown => "unknown",
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct MemoryRegion {
    pub name: &'static str,
    pub kind: RegionKind,
    pub start: u32,
    pub end_inclusive: u32,
    pub instruction_cacheable: bool,
}

impl MemoryRegion {
    pub const fn new(
        name: &'static str,
        kind: RegionKind,
        start: u32,
        end_inclusive: u32,
        instruction_cacheable: bool,
    ) -> Self {
        Self {
            name,
            kind,
            start,
            end_inclusive,
            instruction_cacheable,
        }
    }

    pub fn contains(&self, addr: u32) -> bool {
        self.start <= addr && addr <= self.end_inclusive
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct ClassifiedAddress {
    pub region_name: String,
    pub region_kind: RegionKind,
    pub instruction_cacheable: bool,
    pub recognized_region: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct MachineProfile {
    pub name: &'static str,
    pub description: &'static str,
    pub regions: Vec<MemoryRegion>,
    pub notes: Vec<&'static str>,
}

impl MachineProfile {
    pub fn builtin(name: &str) -> Result<Self> {
        match name {
            "ysyxsoc-current" => Ok(Self::ysyxsoc_current()),
            _ => bail!("unknown machine profile: {name}"),
        }
    }

    pub fn ysyxsoc_current() -> Self {
        Self {
            name: "ysyxsoc-current",
            description: "Current NPC/ysyxSoC instruction-side address policy as implemented by npc/util/SoCMemoryRanges.scala",
            regions: vec![
                MemoryRegion::new("uart", RegionKind::Uart, 0x1000_0000, 0x1000_0fff, false),
                MemoryRegion::new("spi_controller", RegionKind::SpiController, 0x1000_1000, 0x1000_1fff, false),
                MemoryRegion::new("gpio", RegionKind::Gpio, 0x1000_2000, 0x1000_200f, false),
                MemoryRegion::new("keyboard", RegionKind::Keyboard, 0x1001_1000, 0x1001_1007, false),
                MemoryRegion::new("mrom", RegionKind::Mrom, 0x2000_0000, 0x2000_0fff, false),
                MemoryRegion::new("vga", RegionKind::Vga, 0x2100_0000, 0x211f_ffff, false),
                MemoryRegion::new("flash", RegionKind::Flash, 0x3000_0000, 0x3fff_ffff, true),
                MemoryRegion::new("sram", RegionKind::Sram, 0x0f00_0000, 0x0f00_1fff, false),
                MemoryRegion::new("psram", RegionKind::Psram, 0x8000_0000, 0x803f_ffff, true),
                MemoryRegion::new("sdram", RegionKind::Sdram, 0xa000_0000, 0xa7ff_ffff, true),
            ],
            notes: vec![
                "NPC RTL allowlist, not ysyxSoC diplomacy metadata, is authoritative for cachesim cacheability.",
                "Current AM boot flow executes from flash, then SRAM SSBL, then PSRAM/SDRAM application text.",
                "Current refill transport is independent-word 32-bit lower requests, not AXI burst refill.",
            ],
        }
    }

    pub fn classify(&self, addr: u32) -> ClassifiedAddress {
        if let Some(region) = self.regions.iter().find(|region| region.contains(addr)) {
            return ClassifiedAddress {
                region_name: region.name.to_string(),
                region_kind: region.kind,
                instruction_cacheable: region.instruction_cacheable,
                recognized_region: true,
            };
        }

        ClassifiedAddress {
            region_name: "unknown".to_string(),
            region_kind: RegionKind::Unknown,
            instruction_cacheable: false,
            recognized_region: false,
        }
    }
}
