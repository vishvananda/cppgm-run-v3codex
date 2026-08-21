declare function @terminate_now() -> void [role=terminate, unwind=no, return=noreturn]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}
