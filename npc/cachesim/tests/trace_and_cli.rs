use std::io::Write;

use assert_cmd::Command;
use bzip2::write::BzEncoder;
use bzip2::Compression;
use tempfile::tempdir;

const MAGIC: &[u8; 4] = b"PCTR";

fn write_header(out: &mut dyn Write, encoding: u8) {
    out.write_all(MAGIC).unwrap();
    out.write_all(&1_u16.to_le_bytes()).unwrap();
    out.write_all(&16_u16.to_le_bytes()).unwrap();
    out.write_all(&[4, encoding, 1, 0]).unwrap();
    out.write_all(&0_u32.to_le_bytes()).unwrap();
}

#[test]
fn raw_run_and_bzip2_cli_outputs_match() {
    let dir = tempdir().unwrap();
    let raw = dir.path().join("seq.raw.pctrace");
    let run = dir.path().join("seq.run.pctrace");
    let run_bz2 = dir.path().join("seq.run.pctrace.bz2");
    let out_raw = dir.path().join("raw.json");
    let out_run = dir.path().join("run.json");
    let out_bz2 = dir.path().join("run_bz2.json");

    {
        let mut fp = std::fs::File::create(&raw).unwrap();
        write_header(&mut fp, 1);
        for pc in [0x3000_0000_u32, 0x3000_0004, 0x3000_0008, 0x1000_0000] {
            fp.write_all(&pc.to_le_bytes()).unwrap();
        }
    }
    {
        let mut fp = std::fs::File::create(&run).unwrap();
        write_header(&mut fp, 2);
        fp.write_all(&[0x02]).unwrap();
        fp.write_all(&0x3000_0000_u32.to_le_bytes()).unwrap();
        fp.write_all(&3_u32.to_le_bytes()).unwrap();
        fp.write_all(&[0x01]).unwrap();
        fp.write_all(&0x1000_0000_u32.to_le_bytes()).unwrap();
    }
    {
        let file = std::fs::File::create(&run_bz2).unwrap();
        let mut fp = BzEncoder::new(file, Compression::default());
        write_header(&mut fp, 2);
        fp.write_all(&[0x02]).unwrap();
        fp.write_all(&0x3000_0000_u32.to_le_bytes()).unwrap();
        fp.write_all(&3_u32.to_le_bytes()).unwrap();
        fp.write_all(&[0x01]).unwrap();
        fp.write_all(&0x1000_0000_u32.to_le_bytes()).unwrap();
        fp.finish().unwrap();
    }

    for (trace, out) in [(&raw, &out_raw), (&run, &out_run), (&run_bz2, &out_bz2)] {
        Command::cargo_bin("cachesim")
            .unwrap()
            .args([
                "simulate",
                "--trace",
                trace.to_str().unwrap(),
                "--output",
                out.to_str().unwrap(),
            ])
            .assert()
            .success();
    }

    let raw_json: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(out_raw).unwrap()).unwrap();
    let run_json: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(out_run).unwrap()).unwrap();
    let bz2_json: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(out_bz2).unwrap()).unwrap();

    assert_eq!(raw_json["exact"], run_json["exact"]);
    assert_eq!(run_json["exact"], bz2_json["exact"]);
}

#[test]
fn simulate_can_emit_optional_text_summary() {
    let dir = tempdir().unwrap();
    let trace = dir.path().join("trace.pctrace");
    let out_json = dir.path().join("summary.json");
    let out_txt = dir.path().join("summary.txt");

    {
        let mut fp = std::fs::File::create(&trace).unwrap();
        write_header(&mut fp, 1);
        for pc in [
            0x3000_0000_u32,
            0x3000_0004,
            0x3000_0000,
            0x2000_0000,
            0x3000_0010,
        ] {
            fp.write_all(&pc.to_le_bytes()).unwrap();
        }
    }

    Command::cargo_bin("cachesim")
        .unwrap()
        .args([
            "simulate",
            "--trace",
            trace.to_str().unwrap(),
            "--output",
            out_json.to_str().unwrap(),
            "--output-txt",
            out_txt.to_str().unwrap(),
        ])
        .assert()
        .success();

    let summary = std::fs::read_to_string(out_txt).unwrap();
    assert!(summary.contains("NPC CACHE SUMMARY"));
    assert!(summary.contains("I-cache DSE Signals"));
    assert!(summary.contains("3C Miss Breakdown"));
    assert!(summary.contains("Top regions by non-hit pressure"));
}

#[test]
fn elf_bin_invariance_holds() {
    let dir = tempdir().unwrap();
    let trace = dir.path().join("trace.pctrace");
    let out_a = dir.path().join("a.json");
    let out_b = dir.path().join("b.json");
    let dummy_bin = dir.path().join("dummy.bin");
    std::fs::write(&dummy_bin, [1_u8, 2, 3, 4]).unwrap();

    {
        let mut fp = std::fs::File::create(&trace).unwrap();
        write_header(&mut fp, 1);
        for pc in [0x3000_0000_u32, 0x3000_0000, 0x1000_0000, 0x3000_0020] {
            fp.write_all(&pc.to_le_bytes()).unwrap();
        }
    }

    Command::cargo_bin("cachesim")
        .unwrap()
        .args([
            "simulate",
            "--trace",
            trace.to_str().unwrap(),
            "--output",
            out_a.to_str().unwrap(),
        ])
        .assert()
        .success();

    let current_exe = std::env::current_exe().unwrap();
    Command::cargo_bin("cachesim")
        .unwrap()
        .args([
            "simulate",
            "--trace",
            trace.to_str().unwrap(),
            "--elf",
            current_exe.to_str().unwrap(),
            "--bin",
            dummy_bin.to_str().unwrap(),
            "--output",
            out_b.to_str().unwrap(),
        ])
        .assert()
        .success();

    let a: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(out_a).unwrap()).unwrap();
    let b: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(out_b).unwrap()).unwrap();
    assert_eq!(a["exact"], b["exact"]);
    assert_eq!(a["miss_3c"], b["miss_3c"]);
}

#[test]
fn malformed_run_is_rejected() {
    let dir = tempdir().unwrap();
    let trace = dir.path().join("bad.pctrace");
    {
        let mut fp = std::fs::File::create(&trace).unwrap();
        write_header(&mut fp, 2);
        fp.write_all(&[0x02]).unwrap();
        fp.write_all(&0x3000_0000_u32.to_le_bytes()).unwrap();
    }

    Command::cargo_bin("cachesim")
        .unwrap()
        .args(["simulate", "--trace", trace.to_str().unwrap()])
        .assert()
        .failure();
}
