function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $source : obj<8x8>
  slot $destination : obj<8x8>

  block ^entry:
    store i64 7, $source
    %keep0 = copy i64 10
    %keep1 = copy i64 20
    %dividend = copy i64 96
    %quotient = binary div i64 %dividend, 32
    copyobj 8x8 $source, $destination
    %copied = load i64 $destination
    %sum0 = binary add i64 %keep0, %keep1
    %sum1 = binary add i64 %sum0, %quotient
    %sum2 = binary add i64 %sum1, %copied
    %wrong = cmp ne i64 %sum2, 40
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
