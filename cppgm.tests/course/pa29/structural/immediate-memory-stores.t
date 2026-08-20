global @byte_value : u8 = 0
global @short_value : u16 = 0
global @wide_value : i64 = 0

global @words = {
  u32 0
  u32 0
  u32 0
  u32 0
}

function @store_dereference(%address : ptr) -> void {
  block ^entry:
    store u16 4660, %address
    return void
}

function @store_indexed(%base : ptr, %index : i64) -> void {
  block ^entry:
    %address = index u32 %base, %index
    store u32 305419896, %address
    return void
}

function @main() -> i32 [role=entry] {
  slot $inside_i32 : i64
  slot $outside_i32 : i64

  block ^entry:
    store u8 171, @byte_value
    %short_address = addr @short_value
    call void @store_dereference(%short_address)
    %words = addr @words
    call void @store_indexed(%words, 2)
    store i64 2147483647, $inside_i32
    store i64 4294967296, $outside_i32
    store i64 -2147483648, @wide_value

    %byte = load u8 @byte_value
    %short = load u16 @short_value
    %word_address = index u32 %words, 2
    %word = load u32 %word_address
    %inside = load i64 $inside_i32
    %outside = load i64 $outside_i32
    %wide = load i64 @wide_value
    %byte_bad = cmp ne u8 %byte, 171
    %short_bad = cmp ne u16 %short, 4660
    %word_bad = cmp ne u32 %word, 305419896
    %inside_bad = cmp ne i64 %inside, 2147483647
    %outside_bad = cmp ne i64 %outside, 4294967296
    %wide_bad = cmp ne i64 %wide, -2147483648
    %bad1 = binary or i64 %byte_bad, %short_bad
    %bad2 = binary or i64 %word_bad, %inside_bad
    %bad3 = binary or i64 %outside_bad, %wide_bad
    %bad4 = binary or i64 %bad1, %bad2
    %bad = binary or i64 %bad4, %bad3
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
