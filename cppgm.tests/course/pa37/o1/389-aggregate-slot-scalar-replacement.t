declare function @observe(%value : i64) -> void
declare function @escape(%pointer : ptr) -> void

function @decomposes(%dest : ptr, %a : i64, %b : u32, %flag : u8) -> i64 {
  slot $local : obj<24x8>

  block ^entry:
    %base = addr $local
    %f8 = index i8 [projection=field] %base, 8
    %f16 = index i8 [projection=field] %base, 16
    store i64 %a, %base
    store u32 %b, %f16
    branch %flag, ^then, ^join

  block ^then:
    store i64 21, %f8
    jump ^join

  block ^join:
    %first = load i64 %base
    %second = load u32 %f16
    %wide = convert zext i64 u32 %second
    %sum = binary add i64 %first, %wide
    copyobj 24x8 %base, %dest
    return i64 %sum
}

function @copies_between_locals(%a : i64) -> i64 {
  slot $source : obj<16x8>
  slot $target : obj<16x8>

  block ^entry:
    %from = addr $source
    %f8 = index i8 [projection=field] %from, 8
    store i64 %a, %from
    store i64 5, %f8
    %to = addr $target
    copyobj 16x8 %from, %to
    %t8 = index i8 [projection=field] %to, 8
    %low = load i64 %to
    %high = load i64 %t8
    %sum = binary add i64 %low, %high
    return i64 %sum
}

function @escaping_stays(%a : i64) -> i64 {
  slot $local : obj<16x8>

  block ^entry:
    %base = addr $local
    %f8 = index i8 [projection=field] %base, 8
    store i64 %a, %base
    store i64 3, %f8
    call void @escape(%base)
    %read = load i64 %base
    return i64 %read
}
