global @sink : i64 = 0

function @accept(%value : u8) -> i32 {
  block ^entry:
    switch %value, ^bad, 2:^good

  block ^good:
    return i32 0

  block ^bad:
    return i32 1
}

function @get_record(%one : i64, %two : i64, %three : i64, %four : i64,
                     %five : i64, %record : ptr) -> ptr [unwind=no] {
  block ^entry:
    return ptr %record
}

function @reduced(%record : ptr) -> i32 {
  block ^entry:
    %entity = call ptr @get_record(1, 2, 3, 4, 5, %record)
    eh_try ^unwind
    %field = index i8 [projection=field] %entity, 8
    %value = load u8 %field
    %dummy = binary add i64 20, 22
    store i64 %dummy, @sink
    %observed = call i32 @accept(%value)
    eh_end
    return i32 %observed

  block ^unwind:
    resume
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $record : obj<16x8>

  block ^entry:
    %record = addr $record
    %field = index i8 [projection=field] %record, 8
    store u8 2, %field
    %exit = call i32 @reduced(%record)
    return i32 %exit
}
