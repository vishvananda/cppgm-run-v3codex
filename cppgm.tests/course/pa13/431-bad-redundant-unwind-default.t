declare function @ordinary() -> void [unwind=may]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}
