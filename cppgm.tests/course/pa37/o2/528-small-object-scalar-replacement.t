declare function @observe(%value : ptr) -> void [unwind=no]

function @copy_complete_scalar_object() -> i64 [no_inline=yes] {
  slot $source : obj<8x8>
  slot $destination : obj<8x8>

  block ^entry:
    %source_address = addr $source
    %source_field = index i8 [projection=field] %source_address, 0
    store i64 42, %source_field
    %destination_address = addr $destination
    copyobj 8x8 %source_address, %destination_address
    %destination_field = index i8 [projection=field] %destination_address, 0
    %result = load i64 %destination_field
    return i64 %result
}

function @copy_scalar_object_to_external(%destination : ptr) -> void [no_inline=yes] {
  slot $source : obj<8x8>

  block ^entry:
    %source_address = addr $source
    store i64 19, %source_address
    copyobj 8x8 %source_address, %destination
    return void
}

function @keep_escaped_object() -> i64 [no_inline=yes] {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    store i64 7, %address
    call void @observe(%address)
    %result = load i64 %address
    return i64 %result
}

function @keep_partial_object() -> i8 [no_inline=yes] {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    %second_byte = index i8 [projection=field] %address, 1
    store i8 3, %second_byte
    %result = load i8 %second_byte
    return i8 %result
}
