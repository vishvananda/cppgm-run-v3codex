function @main() -> i64 {
  block ^entry:
    %a = const i64 1
    jump ^join

  block ^join:
    %result = phi f64 [^entry: %a]
    %unused = const i64 0
    return i64 %unused
}
