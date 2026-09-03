function @copy_first(%destination : ptr [pass=by_address, object_bytes=24],
                     %source : ptr [pass=by_address, object_bytes=24]) -> void
    [binding=internal, unwind=no] {
  block ^entry:
    %value = load i64 %source
    store i64 %value, %destination
    return void
}

function @call_indirect(%target : ptr,
                        %object : ptr [object_bytes=24]) -> void
    [binding=internal, unwind=no] {
  block ^entry:
    call void %target(%object) as
      (%argument : ptr [pass=by_address, object_bytes=24]) -> void [unwind=no]
    return void
}

function @main() -> i32 [role=entry, unwind=no] {
  slot $left : obj<24x8>
  slot $right : obj<24x8>
  block ^entry:
    %left_address = addr $left
    %right_address = addr $right
    store i64 17, %left_address
    call void @copy_first(%right_address, %left_address)
    %copied = load i64 %right_address
    %bad = cmp ne i64 %copied, 17
    return i32 %bad
}
