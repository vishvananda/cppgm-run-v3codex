global @one = {
  i8 17
}

global @four = {
  i32 572662306
}

global @eight = {
  i64 3689348814741910323
}

global @sixteen = {
  i64 4919131752989213764
  i64 6148914691236517205
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %one_address = addr @one
    zeroinit 1x1 %one_address
    %one_value = load i8 @one
    %one_bad = cmp ne i8 %one_value, 0
    branch %one_bad, ^bad, ^four

  block ^four:
    %four_address = addr @four
    zeroinit 4x4 %four_address
    %four_value = load i32 @four
    %four_bad = cmp ne i32 %four_value, 0
    branch %four_bad, ^bad, ^eight

  block ^eight:
    %eight_address = addr @eight
    zeroinit 8x8 %eight_address
    %eight_value = load i64 @eight
    %eight_bad = cmp ne i64 %eight_value, 0
    branch %eight_bad, ^bad, ^sixteen

  block ^sixteen:
    %sixteen_address = addr @sixteen
    zeroinit 16x8 %sixteen_address
    %sixteen_low = load i64 @sixteen
    %sixteen_high_address = index i64 %sixteen_address, 1
    %sixteen_high = load i64 %sixteen_high_address
    %sixteen_combined = binary or i64 %sixteen_low, %sixteen_high
    %sixteen_bad = cmp ne i64 %sixteen_combined, 0
    branch %sixteen_bad, ^bad, ^ok

  block ^bad:
    return i32 1

  block ^ok:
    return i32 0
}
