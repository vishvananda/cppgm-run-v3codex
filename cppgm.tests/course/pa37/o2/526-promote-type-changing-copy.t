function @choose_unsigned(%take_signed : i64, %signed_value : i32) -> u32 [no_inline=yes] {
  slot $value : u32

  block ^entry:
    branch %take_signed, ^signed, ^constant

  block ^signed:
    %as_unsigned = copy u32 %signed_value
    store u32 %as_unsigned, $value
    jump ^join

  block ^constant:
    store u32 7, $value
    jump ^join

  block ^join:
    %result = load u32 $value
    return u32 %result
}
