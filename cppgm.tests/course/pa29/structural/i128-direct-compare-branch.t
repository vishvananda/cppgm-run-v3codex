function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %result = cmp ge i128 18446744073709551617, 18446744073709551616
    branch %result, ^good, ^bad

  block ^bad:
    return i64 1

  block ^good:
    return i64 0
}
