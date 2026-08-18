global @value : i32 = 17

function @return_constant() -> i32 {
  block ^entry:
    %value = const i32 11
    return i32 %value
}

function @return_load() -> i32 {
  block ^entry:
    %value = load i32 @value
    return i32 %value
}

function @return_global_address() -> ptr {
  block ^entry:
    %address = addr @value
    return ptr %address
}

function @return_slot_address() -> ptr {
  slot $local : i32

  block ^entry:
    %address = addr $local
    return ptr %address
}

function @return_compare(%left : i64, %right : i64) -> i64 {
  block ^entry:
    %result = cmp ult i64 %left, %right
    return i64 %result
}

function @return_negated(%value : i64) -> i64 {
  block ^entry:
    %result = unary neg i64 %value
    return i64 %result
}

function @return_converted(%value : i64) -> i32 {
  block ^entry:
    %result = convert trunc i32 i64 %value
    return i32 %result
}

function @return_quotient(%value : i64) -> i64 {
  block ^entry:
    %result = binary div i64 %value, 7
    return i64 %result
}

function @return_remainder(%value : i64) -> i64 {
  block ^entry:
    %result = binary mod i64 %value, 7
    return i64 %result
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %constant = call i32 @return_constant()
    %loaded = call i32 @return_load()
    %address = call ptr @return_global_address()
    %compared = call i64 @return_compare(3, 5)
    %negated = call i64 @return_negated(-7)
    %converted = call i32 @return_converted(4294967297)
    %quotient = call i64 @return_quotient(50)
    %remainder = call i64 @return_remainder(50)
    %expected = addr @value
    %constant_bad = cmp ne i32 %constant, 11
    %load_bad = cmp ne i32 %loaded, 17
    %address_bad = cmp ne ptr %address, %expected
    %compare_bad = cmp ne i64 %compared, 1
    %negated_bad = cmp ne i64 %negated, 7
    %converted_bad = cmp ne i32 %converted, 1
    %quotient_bad = cmp ne i64 %quotient, 7
    %remainder_bad = cmp ne i64 %remainder, 1
    %first_bad = binary or i64 %constant_bad, %load_bad
    %second_bad = binary or i64 %address_bad, %compare_bad
    %third_bad = binary or i64 %second_bad, %negated_bad
    %fourth_bad = binary or i64 %third_bad, %converted_bad
    %division_bad = binary or i64 %quotient_bad, %remainder_bad
    %fifth_bad = binary or i64 %fourth_bad, %division_bad
    %bad = binary or i64 %first_bad, %fifth_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
