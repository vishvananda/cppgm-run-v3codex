function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

global @global_value : i64 [binding=strong] = 11

function @frame_storage_after_call() -> i64 [unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    store i64 19, %address
    %ignored = call i64 @identity(3)
    %value = load i64 %address
    return i64 %value
}

function @global_storage_after_call() -> i64 [unwind=no] {
  block ^entry:
    %address = addr @global_value
    %ignored = call i64 @identity(5)
    %value = load i64 %address
    return i64 %value
}

function @constant_field_after_call() -> i64 [unwind=no] {
  slot $object : obj<24x8>

  block ^entry:
    %base = addr $object
    %field = index i8 [projection=field] %base, 8
    store i64 23, %field
    %ignored = call i64 @identity(7)
    %value = load i64 %field
    return i64 %value
}

function @variable_index_after_call(%index : i64) -> i64 [unwind=no] {
  slot $object : obj<24x8>

  block ^entry:
    %base = addr $object
    %field = index i64 [projection=array_element] %base, %index
    store i64 29, %field
    %ignored = call i64 @identity(9)
    %value = load i64 %field
    return i64 %value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %frame = call i64 @frame_storage_after_call()
    %global = call i64 @global_storage_after_call()
    %constant = call i64 @constant_field_after_call()
    %variable = call i64 @variable_index_after_call(1)
    %frame_bad = cmp ne i64 %frame, 19
    %global_bad = cmp ne i64 %global, 11
    %constant_bad = cmp ne i64 %constant, 23
    %variable_bad = cmp ne i64 %variable, 29
    %bad0 = binary or i64 %frame_bad, %global_bad
    %bad1 = binary or i64 %constant_bad, %variable_bad
    %bad = binary or i64 %bad0, %bad1
    return i64 %bad
}
