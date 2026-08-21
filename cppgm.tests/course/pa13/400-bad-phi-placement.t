function @main() -> i64 {
  block ^entry:
    %a = const i64 1
    jump ^join

  block ^join:
    %copy = copy i64 %a
    %result = phi i64 [^entry: %a]
    return i64 %result
}
