declare function @undefined_path() -> void [role=unreachable, effects=readnone, unwind=no, return=noreturn]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}
