declare function @mutate(%address : ptr) -> void

function @underflow_shape(%address : ptr) -> i64 {
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

function @scalar_copy_shape(
    %source : ptr [alias=noalias],
    %destination : ptr [alias=noalias]) -> void {
  block ^entry:
    %source8 = index i8 [projection=field] %source, 8
    %destination8 = index i8 [projection=field] %destination, 8
    %value8 = load i64 %source8
    store i64 %value8, %destination8
    %source16 = index i8 [projection=field] %source, 16
    %destination16 = index i8 [projection=field] %destination, 16
    %value16 = load i64 %source16
    store i64 %value16, %destination16
    return void
}

function @zero_shape(%destination : ptr) -> void {
  block ^entry:
    zeroinit 16x8 %destination
    %high = index i8 [projection=field] %destination, 8
    store i64 11, %destination
    store i64 22, %high
    return void
}
