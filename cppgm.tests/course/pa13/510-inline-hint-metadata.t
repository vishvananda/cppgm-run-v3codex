function @preferred(%value : i64) -> i64 [binding=weak, inline_hint=yes] {
  block ^entry:
    return i64 %value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}
