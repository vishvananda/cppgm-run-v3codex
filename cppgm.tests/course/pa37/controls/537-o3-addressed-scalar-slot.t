function @recover_direct(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 %input, $value
    %address = addr $value
    %result = load i64 %address
    return i64 %result
}

function @recover_pointer_chain(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 %input, $value
    %address = addr $value
    %copy = copy ptr %address
    %zero = index i8 %copy, 0
    %result = load i64 %zero
    return i64 %result
}

function @recover_addressed_store(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    store i64 %input, %address
    %result = load i64 $value
    return i64 %result
}

function @recover_complete_copy(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $source : i64
  slot $destination : i64

  block ^entry:
    store i64 %input, $source
    %source_address = addr $source
    %destination_address = addr $destination
    copyobj 8x8 %source_address, %destination_address
    %result = load i64 %destination_address
    return i64 %result
}

function @recover_complete_zero() -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    zeroinit 8x8 %address
    %result = load i64 %address
    return i64 %result
}

function @mutate(%address : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    store i64 17, %address
    return void
}

function @keep_escaped_address(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 %input, $value
    %address = addr $value
    call void @mutate(%address)
    %result = load i64 %address
    return i64 %result
}

function @keep_nonzero_index() -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 0, $value
    %address = addr $value
    %second_byte = index i8 %address, 1
    store i8 1, %second_byte
    %result = load i64 %address
    return i64 %result
}

function @keep_variable_index(%offset : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 0, $value
    %address = addr $value
    %selected = index i8 %address, %offset
    store i8 7, %selected
    %result = load i64 %address
    return i64 %result
}

function @keep_volatile_access(%input : i64) -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    store volatile i64 %input, %address
    %result = load volatile i64 %address
    return i64 %result
}

function @keep_partial_type() -> i64 [no_inline=yes, unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    store i64 258, %address
    %partial = load u32 %address
    %result = convert zext i64 u32 %partial
    return i64 %result
}

function @main() -> i32 {
  block ^entry:
    %direct = call i64 @recover_direct(41)
    %direct_ok = cmp eq i64 %direct, 41
    %chain = call i64 @recover_pointer_chain(42)
    %chain_ok = cmp eq i64 %chain, 42
    %first_pair = binary and i64 %direct_ok, %chain_ok

    %addressed_store = call i64 @recover_addressed_store(43)
    %addressed_store_ok = cmp eq i64 %addressed_store, 43
    %complete_copy = call i64 @recover_complete_copy(44)
    %complete_copy_ok = cmp eq i64 %complete_copy, 44
    %second_pair = binary and i64 %addressed_store_ok, %complete_copy_ok
    %first_group = binary and i64 %first_pair, %second_pair

    %complete_zero = call i64 @recover_complete_zero()
    %complete_zero_ok = cmp eq i64 %complete_zero, 0
    %escaped = call i64 @keep_escaped_address(45)
    %escaped_ok = cmp eq i64 %escaped, 17
    %third_pair = binary and i64 %complete_zero_ok, %escaped_ok

    %nonzero = call i64 @keep_nonzero_index()
    %nonzero_ok = cmp eq i64 %nonzero, 256
    %variable = call i64 @keep_variable_index(0)
    %variable_ok = cmp eq i64 %variable, 7
    %fourth_pair = binary and i64 %nonzero_ok, %variable_ok
    %second_group = binary and i64 %third_pair, %fourth_pair

    %volatile = call i64 @keep_volatile_access(47)
    %volatile_ok = cmp eq i64 %volatile, 47
    %partial = call i64 @keep_partial_type()
    %partial_ok = cmp eq i64 %partial, 258
    %last_pair = binary and i64 %volatile_ok, %partial_ok

    %first_eight = binary and i64 %first_group, %second_group
    %all_ok = binary and i64 %first_eight, %last_pair
    branch %all_ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
