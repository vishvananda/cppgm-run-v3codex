global @global_rhs : i64 = 9
global @xor_rhs : i64 = 240
global @byte_rhs : i8 = 3

global @words = {
  i64 2
  i64 4
  i64 77
}

function @add_global(%left : i64) -> i64 {
  block ^entry:
    %right = load i64 @global_rhs
    %result = binary add i64 %left, %right
    return i64 %result
}

function @sub_frame(%left : i64, %unused1 : i64, %unused2 : i64, %unused3 : i64, %unused4 : i64, %unused5 : i64, %right : i64) -> i64 {
  block ^entry:
    %result = binary sub i64 %left, %right
    return i64 %result
}

function @and_dereference(%address : ptr, %left : i64) -> i64 {
  block ^entry:
    %right = load i64 %address
    %result = binary and i64 %left, %right
    return i64 %result
}

function @or_indexed(%base : ptr, %index_value : i64, %left : i64) -> i64 {
  block ^entry:
    %address = index i64 %base, %index_value
    %right = load i64 %address
    %result = binary or i64 %left, %right
    return i64 %result
}

function @xor_global(%left : i64) -> i64 {
  block ^entry:
    %right = load i64 @xor_rhs
    %result = binary xor i64 %left, %right
    return i64 %result
}

function @multiply_frame(%left : i32, %unused1 : i32, %unused2 : i32, %unused3 : i32, %unused4 : i32, %unused5 : i32, %right : i32) -> i32 {
  block ^entry:
    %result = binary mul i32 %left, %right
    return i32 %result
}

function @compare_indexed(%base : ptr, %index_value : i64, %left : i64) -> i64 {
  block ^entry:
    %address = index i64 %base, %index_value
    %right = load i64 %address
    %result = cmp eq i64 %left, %right
    return i64 %result
}

function @divide_dereference(%address : ptr, %left : i64) -> i64 {
  block ^entry:
    %right = load i64 %address
    %result = binary div i64 %left, %right
    return i64 %result
}

function @shift_frame(%left : i64, %unused1 : i64, %unused2 : i64, %unused3 : i64, %unused4 : i64, %unused5 : i64, %count : i64) -> i64 {
  block ^entry:
    %result = binary shl i64 %left, %count
    return i64 %result
}

function @multiply_byte(%address : ptr, %left : i8) -> i8 {
  block ^entry:
    %right = load i8 %address
    %result = binary mul i8 %left, %right
    return i8 %result
}

function @add_byte_indexed(%base : ptr, %index_value : i64, %left : i8) -> i8 {
  block ^entry:
    %address = index i8 %base, %index_value
    %right = load i8 %address
    %result = binary add i8 %left, %right
    return i8 %result
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %global_address = addr @global_rhs
    %byte_address = addr @byte_rhs
    %word_address = addr @words
    %add = call i64 @add_global(5)
    %sub = call i64 @sub_frame(20, 0, 0, 0, 0, 0, 3)
    %and = call i64 @and_dereference(%global_address, 15)
    %or = call i64 @or_indexed(%word_address, 1, 1)
    %xor = call i64 @xor_global(15)
    %multiply = call i32 @multiply_frame(6, 0, 0, 0, 0, 0, 7)
    %compare = call i64 @compare_indexed(%word_address, 2, 77)
    %divide = call i64 @divide_dereference(%global_address, 81)
    %shift = call i64 @shift_frame(3, 0, 0, 0, 0, 0, 2)
    %byte = call i8 @multiply_byte(%byte_address, 5)
    %byte_indexed = call i8 @add_byte_indexed(%byte_address, 0, 4)
    %bad_add = cmp ne i64 %add, 14
    %bad_sub = cmp ne i64 %sub, 17
    %bad_and = cmp ne i64 %and, 9
    %bad_or = cmp ne i64 %or, 5
    %bad_xor = cmp ne i64 %xor, 255
    %bad_multiply = cmp ne i32 %multiply, 42
    %bad_compare = cmp ne i64 %compare, 1
    %bad_divide = cmp ne i64 %divide, 9
    %bad_shift = cmp ne i64 %shift, 12
    %bad_byte = cmp ne i8 %byte, 15
    %bad_byte_indexed = cmp ne i8 %byte_indexed, 7
    %bad1 = binary or i64 %bad_add, %bad_sub
    %bad2 = binary or i64 %bad_and, %bad_or
    %bad3 = binary or i64 %bad_xor, %bad_multiply
    %bad4 = binary or i64 %bad_compare, %bad_divide
    %bad_byte_all = binary or i64 %bad_byte, %bad_byte_indexed
    %bad5 = binary or i64 %bad_shift, %bad_byte_all
    %bad6 = binary or i64 %bad1, %bad2
    %bad7 = binary or i64 %bad3, %bad4
    %bad8 = binary or i64 %bad6, %bad7
    %bad = binary or i64 %bad8, %bad5
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
