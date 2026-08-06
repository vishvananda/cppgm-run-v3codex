declare function @external_entry() -> i64 [role=entry]

function @main() -> i64 {
  block ^entry:
    return i64 0
}
