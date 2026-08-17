function @main() -> i64 [role=entry] {
  block ^entry:
    %constant = const i64 40
    %copied = copy i64 %constant
    %result = binary add i64 %copied, 2
    return i64 %result
}
