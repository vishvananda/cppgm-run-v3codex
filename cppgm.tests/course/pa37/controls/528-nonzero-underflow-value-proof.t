function @fold_dominating_same_value(%value : u32) -> i64 {
  block ^entry:
    %nonzero = cmp ne u32 %value, 0
    branch %nonzero, ^through, ^zero

  block ^through:
    jump ^check

  block ^check:
    %decremented = binary sub u32 %value, 1
    %underflowed = cmp uge u32 %decremented, %value
    branch %underflowed, ^bad, ^good

  block ^zero:
    return i64 0

  block ^bad:
    return i64 1

  block ^good:
    return i64 2
}

function @retain_different_nonzero_value(%guard : u32, %value : u32) -> i64 {
  block ^entry:
    %nonzero = cmp ne u32 %guard, 0
    branch %nonzero, ^check, ^zero

  block ^check:
    %decremented = binary sub u32 %value, 1
    %underflowed = cmp uge u32 %decremented, %value
    branch %underflowed, ^bad, ^good

  block ^zero:
    return i64 0

  block ^bad:
    return i64 1

  block ^good:
    return i64 2
}
