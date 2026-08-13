use cachesim::cache::{CacheAccessKind, CacheConfig, CacheModel, MissKind3C};
use cachesim::replacement::ReplacementPolicy;

#[test]
fn cold_miss_then_hit() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    let first = cache.access(0x8000_0000, true);
    let second = cache.access(0x8000_0000, true);

    assert_eq!(first.kind, CacheAccessKind::Miss);
    assert_eq!(first.miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(second.kind, CacheAccessKind::Hit);
}

#[test]
fn direct_map_conflict_is_miss_miss_miss() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    let a = 0x8000_0000;
    let b = 0x8000_0020;
    assert_eq!(cache.access(a, true).kind, CacheAccessKind::Miss);
    assert_eq!(cache.access(b, true).kind, CacheAccessKind::Miss);
    assert_eq!(cache.access(a, true).kind, CacheAccessKind::Miss);
}

#[test]
fn independent_indices_coexist() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    assert_eq!(cache.access(0x3000_0000, true).kind, CacheAccessKind::Miss);
    assert_eq!(cache.access(0x3000_0004, true).kind, CacheAccessKind::Miss);
    assert_eq!(cache.access(0x3000_0000, true).kind, CacheAccessKind::Hit);
}

#[test]
fn bypass_never_allocates() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    assert_eq!(cache.access(0x1000_0000, false).kind, CacheAccessKind::Bypass);
    assert_eq!(cache.access(0x1000_0000, false).kind, CacheAccessKind::Bypass);
}

#[test]
fn multiword_spatial_locality_and_line_base_behavior() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 32,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    let base = 0x8000_0000;
    assert_eq!(cache.access(base + 20, true).kind, CacheAccessKind::Miss);
    for offset in [0, 4, 8, 12, 16, 20, 24, 28] {
        assert_eq!(cache.access(base + offset, true).kind, CacheAccessKind::Hit);
    }
}

#[test]
fn lru_and_fifo_choose_different_victim() {
    let cfg = CacheConfig {
        block_bytes: 4,
        total_lines: 4,
        ways: 2,
        replacement: ReplacementPolicy::Lru,
    };
    let mut lru = CacheModel::new(cfg);
    let mut fifo = CacheModel::new(CacheConfig {
        replacement: ReplacementPolicy::Fifo,
        ..cfg
    });

    let a = 0x8000_0000;
    let b = 0x8000_0008;
    let c = 0x8000_0010;

    for model in [&mut lru, &mut fifo] {
        assert_eq!(model.access(a, true).kind, CacheAccessKind::Miss);
        assert_eq!(model.access(b, true).kind, CacheAccessKind::Miss);
        assert_eq!(model.access(a, true).kind, CacheAccessKind::Hit);
        assert_eq!(model.access(c, true).kind, CacheAccessKind::Miss);
    }

    assert_eq!(lru.access(a, true).kind, CacheAccessKind::Hit);
    assert_eq!(lru.access(b, true).kind, CacheAccessKind::Miss);

    assert_eq!(fifo.access(a, true).kind, CacheAccessKind::Miss);

    let mut fifo = CacheModel::new(CacheConfig {
        replacement: ReplacementPolicy::Fifo,
        ..cfg
    });
    assert_eq!(fifo.access(a, true).kind, CacheAccessKind::Miss);
    assert_eq!(fifo.access(b, true).kind, CacheAccessKind::Miss);
    assert_eq!(fifo.access(a, true).kind, CacheAccessKind::Hit);
    assert_eq!(fifo.access(c, true).kind, CacheAccessKind::Miss);
    assert_eq!(fifo.access(b, true).kind, CacheAccessKind::Hit);
}

#[test]
fn one_way_replacement_policy_is_irrelevant() {
    let sequence = [0x8000_0000, 0x8000_0020, 0x8000_0000, 0x8000_0004];
    let mut lru = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });
    let mut fifo = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 8,
        ways: 1,
        replacement: ReplacementPolicy::Fifo,
    });

    let lru_results: Vec<_> = sequence.iter().map(|pc| lru.access(*pc, true).kind).collect();
    let fifo_results: Vec<_> = sequence.iter().map(|pc| fifo.access(*pc, true).kind).collect();
    assert_eq!(lru_results, fifo_results);
}

#[test]
fn three_c_classification_works() {
    let mut cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 2,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });

    assert_eq!(cache.access(0x8000_0000, true).miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(cache.access(0x8000_0008, true).miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(cache.access(0x8000_0000, true).miss_3c, Some(MissKind3C::Conflict));

    let mut capacity_cache = CacheModel::new(CacheConfig {
        block_bytes: 4,
        total_lines: 2,
        ways: 1,
        replacement: ReplacementPolicy::Lru,
    });
    assert_eq!(capacity_cache.access(0x8000_0000, true).miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(capacity_cache.access(0x8000_0004, true).miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(capacity_cache.access(0x8000_0008, true).miss_3c, Some(MissKind3C::Compulsory));
    assert_eq!(capacity_cache.access(0x8000_0000, true).miss_3c, Some(MissKind3C::Capacity));
}
