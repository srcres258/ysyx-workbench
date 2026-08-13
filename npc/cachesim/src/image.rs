use std::fs::File;
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use object::{Object, ObjectSection};
use serde::Serialize;
use sha2::{Digest, Sha256};

#[derive(Debug, Clone, Serialize)]
pub struct ArtifactIdentity {
    pub path: PathBuf,
    pub sha256: String,
    pub size_bytes: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct ExecutableSection {
    pub name: String,
    pub start: u64,
    pub end_exclusive: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct ElfInfo {
    pub identity: ArtifactIdentity,
    pub executable_sections: Vec<ExecutableSection>,
}

impl ElfInfo {
    pub fn find_section(&self, pc: u32) -> Option<&str> {
        let pc = pc as u64;
        self.executable_sections
            .iter()
            .find(|section| section.start <= pc && pc < section.end_exclusive)
            .map(|section| section.name.as_str())
    }
}

pub fn hash_file(path: &Path) -> Result<ArtifactIdentity> {
    let file = File::open(path).with_context(|| format!("failed to open file: {}", path.display()))?;
    let metadata = file
        .metadata()
        .with_context(|| format!("failed to stat file: {}", path.display()))?;
    let mut reader = BufReader::new(file);
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 8192];
    loop {
        let count = reader.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }
    Ok(ArtifactIdentity {
        path: path.to_path_buf(),
        sha256: hex::encode(hasher.finalize()),
        size_bytes: metadata.len(),
    })
}

pub fn load_elf_info(path: &Path) -> Result<ElfInfo> {
    let identity = hash_file(path)?;
    let bytes = std::fs::read(path).with_context(|| format!("failed to read ELF: {}", path.display()))?;
    let object = object::File::parse(&*bytes)
        .with_context(|| format!("failed to parse ELF/object file: {}", path.display()))?;

    let mut executable_sections = object
        .sections()
        .filter(|section| section.address() != 0 && section.size() != 0)
        .filter_map(|section| {
            let flags = section.flags();
            let is_executable = match flags {
                object::SectionFlags::Elf { sh_flags } => {
                    sh_flags & (object::elf::SHF_EXECINSTR as u64) != 0
                }
                _ => false,
            };
            if !is_executable {
                return None;
            }
            Some(ExecutableSection {
                name: section.name().ok()?.to_string(),
                start: section.address(),
                end_exclusive: section.address() + section.size(),
            })
        })
        .collect::<Vec<_>>();
    executable_sections.sort_by_key(|section| section.start);

    Ok(ElfInfo {
        identity,
        executable_sections,
    })
}
