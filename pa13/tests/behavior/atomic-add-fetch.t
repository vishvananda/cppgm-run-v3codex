global @counter : i64 = 7

function @main() -> i64 [role=entry] {
  block ^entry:
    %slot = addr @counter
    %delta = const i64 5
    %updated = atomic_add_fetch i64 %slot, %delta, 5
    %stored = atomic_load i64 %slot, 5
    %expected = const i64 12
    %updated_bad = cmp ne i64 %updated, %expected
    %stored_bad = cmp ne i64 %stored, %expected
    %failed = binary or i1 %updated_bad, %stored_bad
    %status = convert zext i64 i1 %failed
    return i64 %status
}
