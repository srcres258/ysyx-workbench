use std::fs::File;
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use bzip2::read::BzDecoder;
use serde::Serialize;
use thiserror::Error;

pub const PCTR_MAGIC: [u8; 4] = *b"PCTR";
pub const PCTR_V1_VERSION: u16 = 1;
pub const PCTR_V1_HEADER_SIZE: u16 = 16;
pub const PCTR_V1_ADDRESS_WIDTH: u8 = 4;
pub const PCTR_ENDIANNESS_LITTLE: u8 = 1;
pub const PCTR_V1_TAG_SINGLE_PC: u8 = 0x01;
pub const PCTR_V1_TAG_RUN: u8 = 0x02;
pub const PCTR_V1_RUN_STRIDE: u32 = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TraceEncoding {
    Raw,
    Run,
}

impl TraceEncoding {
    fn from_u8(value: u8) -> Result<Self, TraceFormatError> {
        match value {
            1 => Ok(Self::Raw),
            2 => Ok(Self::Run),
            _ => Err(TraceFormatError::UnsupportedEncoding(value)),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TraceCompression {
    None,
    Bzip2,
}

#[derive(Debug, Clone, Serialize)]
pub struct TraceHeader {
    pub version: u16,
    pub header_size: u16,
    pub address_width: u8,
    pub encoding: TraceEncoding,
    pub endianness: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TraceRecord {
    SinglePc(u32),
    Run { start_pc: u32, count: u32 },
}

#[derive(Debug, Error)]
pub enum TraceFormatError {
    #[error("trace header is truncated")]
    TruncatedHeader,
    #[error("bad magic: {0:?}")]
    BadMagic([u8; 4]),
    #[error("unsupported version: {0}")]
    UnsupportedVersion(u16),
    #[error("unsupported header size: {0}")]
    UnsupportedHeaderSize(u16),
    #[error("unsupported address width: {0}")]
    UnsupportedAddressWidth(u8),
    #[error("unsupported endianness flag: {0}")]
    UnsupportedEndianness(u8),
    #[error("unsupported encoding: {0}")]
    UnsupportedEncoding(u8),
    #[error("reserved header fields must be zero")]
    ReservedFieldsNonZero,
    #[error("raw pc record is truncated")]
    TruncatedRawRecord,
    #[error("single-pc record is truncated")]
    TruncatedSinglePcRecord,
    #[error("seq-run record is truncated")]
    TruncatedRunRecord,
    #[error("unknown run-encoding tag: 0x{0:02x}")]
    UnknownRunTag(u8),
    #[error("run record with zero count")]
    ZeroCountRun,
}

fn read_u16_le(bytes: &[u8]) -> u16 {
    u16::from_le_bytes([bytes[0], bytes[1]])
}

fn read_u32_le(bytes: &[u8]) -> u32 {
    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

fn read_exact_or_truncated(reader: &mut dyn Read, buffer: &mut [u8], truncated: TraceFormatError) -> Result<bool> {
    let mut offset = 0;
    while offset < buffer.len() {
        let count = reader.read(&mut buffer[offset..])?;
        if count == 0 {
            if offset == 0 {
                return Ok(false);
            }
            return Err(truncated.into());
        }
        offset += count;
    }
    Ok(true)
}

fn open_trace_reader(path: &Path) -> Result<(Box<dyn Read>, TraceCompression)> {
    let mut sniff = File::open(path)
        .with_context(|| format!("failed to open trace for sniffing: {}", path.display()))?;
    let mut prefix = [0_u8; 3];
    let count = sniff
        .read(&mut prefix)
        .with_context(|| format!("failed to read trace prefix: {}", path.display()))?;

    let file = File::open(path)
        .with_context(|| format!("failed to open trace: {}", path.display()))?;
    let reader = BufReader::new(file);

    if count == 3 && prefix == *b"BZh" {
        Ok((Box::new(BzDecoder::new(reader)), TraceCompression::Bzip2))
    } else {
        Ok((Box::new(reader), TraceCompression::None))
    }
}

pub struct TraceReader {
    path: PathBuf,
    reader: Box<dyn Read>,
    compression: TraceCompression,
    header: TraceHeader,
}

impl TraceReader {
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_path_buf();
        let (mut reader, compression) = open_trace_reader(&path)?;
        let header = Self::read_header(&mut reader)?;
        Ok(Self {
            path,
            reader,
            compression,
            header,
        })
    }

    fn read_header(reader: &mut dyn Read) -> Result<TraceHeader> {
        let mut header = [0_u8; PCTR_V1_HEADER_SIZE as usize];
        if !read_exact_or_truncated(reader, &mut header, TraceFormatError::TruncatedHeader)? {
            return Err(TraceFormatError::TruncatedHeader.into());
        }

        let magic = [header[0], header[1], header[2], header[3]];
        let version = read_u16_le(&header[4..6]);
        let header_size = read_u16_le(&header[6..8]);
        let address_width = header[8];
        let encoding = TraceEncoding::from_u8(header[9])?;
        let endianness = header[10];
        let reserved0 = header[11];
        let reserved1 = read_u32_le(&header[12..16]);

        if magic != PCTR_MAGIC {
            return Err(TraceFormatError::BadMagic(magic).into());
        }
        if version != PCTR_V1_VERSION {
            return Err(TraceFormatError::UnsupportedVersion(version).into());
        }
        if header_size != PCTR_V1_HEADER_SIZE {
            return Err(TraceFormatError::UnsupportedHeaderSize(header_size).into());
        }
        if address_width != PCTR_V1_ADDRESS_WIDTH {
            return Err(TraceFormatError::UnsupportedAddressWidth(address_width).into());
        }
        if endianness != PCTR_ENDIANNESS_LITTLE {
            return Err(TraceFormatError::UnsupportedEndianness(endianness).into());
        }
        if reserved0 != 0 || reserved1 != 0 {
            return Err(TraceFormatError::ReservedFieldsNonZero.into());
        }

        Ok(TraceHeader {
            version,
            header_size,
            address_width,
            encoding,
            endianness,
        })
    }

    pub fn header(&self) -> &TraceHeader {
        &self.header
    }

    pub fn compression(&self) -> TraceCompression {
        self.compression
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn next_record(&mut self) -> Result<Option<TraceRecord>> {
        match self.header.encoding {
            TraceEncoding::Raw => {
                let mut chunk = [0_u8; 4];
                if !read_exact_or_truncated(&mut *self.reader, &mut chunk, TraceFormatError::TruncatedRawRecord)? {
                    return Ok(None);
                }
                Ok(Some(TraceRecord::SinglePc(u32::from_le_bytes(chunk))))
            }
            TraceEncoding::Run => {
                let mut tag = [0_u8; 1];
                if !read_exact_or_truncated(&mut *self.reader, &mut tag, TraceFormatError::TruncatedRunRecord)? {
                    return Ok(None);
                }
                match tag[0] {
                    PCTR_V1_TAG_SINGLE_PC => {
                        let mut chunk = [0_u8; 4];
                        read_exact_or_truncated(&mut *self.reader, &mut chunk, TraceFormatError::TruncatedSinglePcRecord)?;
                        Ok(Some(TraceRecord::SinglePc(u32::from_le_bytes(chunk))))
                    }
                    PCTR_V1_TAG_RUN => {
                        let mut chunk = [0_u8; 8];
                        read_exact_or_truncated(&mut *self.reader, &mut chunk, TraceFormatError::TruncatedRunRecord)?;
                        let start_pc = u32::from_le_bytes(chunk[0..4].try_into().unwrap());
                        let count = u32::from_le_bytes(chunk[4..8].try_into().unwrap());
                        if count == 0 {
                            return Err(TraceFormatError::ZeroCountRun.into());
                        }
                        Ok(Some(TraceRecord::Run { start_pc, count }))
                    }
                    other => Err(TraceFormatError::UnknownRunTag(other).into()),
                }
            }
        }
    }
}
