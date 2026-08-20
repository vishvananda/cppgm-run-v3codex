global @record = {
  zero 41
}

function @identity(%record : ptr) -> ptr {
  block ^entry:
    return ptr %record
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %record = addr @record
    %returned = call ptr @identity(%record)
    %flag = index i8 [projection=field] %returned, 40
    store u8 1, %flag
    return i32 0
}
