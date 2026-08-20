function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $a : i64
  slot $b : i64
  slot $c : i64
  slot $d : i64
  slot $object : obj<8x8>

  block ^entry:
    store i64 10, $a
    store i64 20, $b
    store i64 30, $c
    store i64 40, $d
    store i64 7, $object
    %keep0 = load i64 $a
    %keep1 = load i64 $b
    %keep2 = load i64 $c
    %keep3 = load i64 $d
    %unused = load obj<8x8> $object
    %sum0 = binary add i64 %keep0, %keep1
    %sum1 = binary add i64 %sum0, %keep2
    %sum2 = binary add i64 %sum1, %keep3
    %wrong = cmp ne i64 %sum2, 100
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
