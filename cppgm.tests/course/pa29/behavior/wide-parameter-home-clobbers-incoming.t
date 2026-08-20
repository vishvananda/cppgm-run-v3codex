function @combine_after_copy(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64) -> i64 {
  slot $source : obj<8x8>
  slot $destination : obj<8x8>

  block ^entry:
    store i64 1, $source
    copyobj 8x8 $source, $destination
    %sum = binary add i64 %b, %f
    return i64 %sum
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @combine_after_copy(1, 2, 3, 4, 5, 6)
    %wrong = cmp ne i64 %actual, 8
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
