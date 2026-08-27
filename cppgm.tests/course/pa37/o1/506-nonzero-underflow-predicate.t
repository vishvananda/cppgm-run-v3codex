declare function @mutate(%address : ptr) -> void

function @fold_after_nonzero_edge(%address : ptr) -> i64 {
  block ^entry:
    %initial = load u32 %address
    %nonzero = cmp ne u32 %initial, 0
    branch %nonzero, ^check, ^zero

  block ^check:
    %reloaded = load u32 %address
    %decremented = binary sub u32 %reloaded, 1
    %underflowed = cmp uge u32 %decremented, %reloaded
    branch %underflowed, ^bad, ^good

  block ^zero:
    return i64 0

  block ^bad:
    return i64 1

  block ^good:
    return i64 2
}

function @retain_after_mutation(%address : ptr) -> i64 {
  block ^entry:
    %initial = load u32 %address
    %nonzero = cmp ne u32 %initial, 0
    branch %nonzero, ^check, ^zero

  block ^check:
    call void @mutate(%address)
    %reloaded = load u32 %address
    %decremented = binary sub u32 %reloaded, 1
    %underflowed = cmp uge u32 %decremented, %reloaded
    branch %underflowed, ^bad, ^good

  block ^zero:
    return i64 0

  block ^bad:
    return i64 1

  block ^good:
    return i64 2
}

function @retain_volatile_reload(%address : ptr) -> i64 {
  block ^entry:
    %initial = load volatile u32 %address
    %nonzero = cmp ne u32 %initial, 0
    branch %nonzero, ^check, ^zero

  block ^check:
    %reloaded = load volatile u32 %address
    %decremented = binary sub u32 %reloaded, 1
    %underflowed = cmp uge u32 %decremented, %reloaded
    branch %underflowed, ^bad, ^good

  block ^zero:
    return i64 0

  block ^bad:
    return i64 1

  block ^good:
    return i64 2
}
