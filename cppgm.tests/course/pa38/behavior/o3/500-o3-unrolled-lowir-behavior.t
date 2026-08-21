global @observed : i64 = 0

function @main() -> i64 [role=entry] {
  block ^entry:
    store i64 0, @observed
    store i64 1, @observed
    store i64 3, @observed
    store i64 6, @observed
    %value = load i64 @observed
    %bad = cmp ne i64 %value, 6
    return i64 %bad
}
