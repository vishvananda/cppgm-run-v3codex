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

function @main() -> i32 [role=entry] {
  block ^entry:
    %constant = call i32 @return_constant()
    %loaded = call i32 @return_load()
    %address = call ptr @return_global_address()
    %expected = addr @value
    %constant_bad = cmp ne i32 %constant, 11
    %load_bad = cmp ne i32 %loaded, 17
    %address_bad = cmp ne ptr %address, %expected
    %first_bad = binary or i64 %constant_bad, %load_bad
    %bad = binary or i64 %first_bad, %address_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
