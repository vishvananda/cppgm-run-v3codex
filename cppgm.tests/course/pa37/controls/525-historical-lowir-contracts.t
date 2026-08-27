global @readonly_value : i64 [storage=readonly] = 7
global @loop_result : i64 = 0

function @fold_readonly() -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 @readonly_value
    return i64 %value
}

function @strength_reduce() -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    jump ^header

  block ^header:
    %index = phi i64 [^entry: 0, ^latch: %next]
    %done = cmp ge i64 %index, 4
    branch %done, ^exit, ^body

  block ^body:
    %scaled = binary mul i64 %index, 8
    store i64 %scaled, @loop_result
    jump ^latch

  block ^latch:
    %next = binary add i64 %index, 1
    jump ^header

  block ^exit:
    %answer = load i64 @loop_result
    return i64 %answer
}

function @factor_scaled_index(%index : i64) -> i64
    [no_inline=yes, unwind=no] {
  slot $scaled_storage : obj<64x8>

  block ^entry:
    %base = addr $scaled_storage
    %scaled = binary mul i64 %index, 24
    %element = index i8 [projection=array_element] %base, %scaled
    store volatile i64 91, %element
    %value = load volatile i64 %element
    return i64 %value
}

function @retain_multi_use_scale(%index : i64) -> i64
    [no_inline=yes, unwind=no] {
  slot $scaled_storage : obj<64x8>

  block ^entry:
    %base = addr $scaled_storage
    %scaled = binary mul i64 %index, 24
    %element = index i8 [projection=array_element] %base, %scaled
    store volatile i64 17, %element
    return i64 %scaled
}

function @retain_unfactorable_scale(%index : i64) -> i64
    [no_inline=yes, unwind=no] {
  slot $scaled_storage : obj<64x8>

  block ^entry:
    %base = addr $scaled_storage
    %scaled = binary mul i64 %index, 3
    %element = index i8 [projection=array_element] %base, %scaled
    store volatile i64 73, %element
    %value = load volatile i64 %element
    return i64 %value
}

function @promote_complete_object() -> i64 [no_inline=yes, unwind=no] {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    store i64 42, %address
    %result = load i64 %address
    return i64 %result
}

function @retain_object_value_copy(%source : obj<8x8>) -> i64
    [no_inline=yes, unwind=no] {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    copyobj 8x8 %source, %address
    %result = load i64 %address
    return i64 %result
}

function @promote_scalar_slot(%value : i64) -> i64
    [no_inline=yes, unwind=no] {
  slot $scalar : i64

  block ^entry:
    store i64 3, $scalar
    store i64 %value, $scalar
    %result = load i64 $scalar
    return i64 %result
}

function @observe_scalar_slot(%address : ptr) -> i64
    [no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 %address
    return i64 %value
}

function @retain_escaped_scalar_slot() -> i64
    [no_inline=yes, unwind=no] {
  slot $scalar : i64

  block ^entry:
    store i64 11, $scalar
    %address = addr $scalar
    %result = call i64 @observe_scalar_slot(%address)
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %readonly = call i64 @fold_readonly()
    %strength = call i64 @strength_reduce()
    %factored = call i64 @factor_scaled_index(1)
    %multi = call i64 @retain_multi_use_scale(1)
    %unfactorable = call i64 @retain_unfactorable_scale(1)
    %object = call i64 @promote_complete_object()
	%scalar = call i64 @promote_scalar_slot(13)
	%escaped = call i64 @retain_escaped_scalar_slot()
    %bad0 = cmp ne i64 %readonly, 7
    %bad1 = cmp ne i64 %strength, 24
    %bad5 = cmp ne i64 %factored, 91
    %bad6 = cmp ne i64 %multi, 24
    %bad7 = cmp ne i64 %unfactorable, 73
    %bad2 = cmp ne i64 %object, 42
	%bad3 = cmp ne i64 %scalar, 13
	%bad4 = cmp ne i64 %escaped, 11
    %bad01 = binary or i64 %bad0, %bad1
	%bad23 = binary or i64 %bad2, %bad3
	%bad04 = binary or i64 %bad01, %bad23
	%bad56 = binary or i64 %bad5, %bad6
	%bad057 = binary or i64 %bad04, %bad56
	%bad047 = binary or i64 %bad057, %bad4
	%bad = binary or i64 %bad047, %bad7
    return i64 %bad
}
