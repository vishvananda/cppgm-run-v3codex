function @main() -> i64 [role=entry] {
  block ^entry:
    eh_try ^handler
    throw i64 7

  block ^handler:
    %value = exception i64
    %result = binary sub i64 %value, 7
    return i64 %result
}
