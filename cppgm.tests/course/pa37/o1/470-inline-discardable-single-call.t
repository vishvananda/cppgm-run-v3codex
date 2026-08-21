global @sink : i64 = 0

function @single(%value : i64) -> i64 [binding=weak, unwind=no] {
  block ^entry:
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    return i64 %value
}

function @multiple(%value : i64) -> i64 [binding=weak, unwind=no] {
  block ^entry:
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    return i64 %value
}

function @addressed(%value : i64) -> i64 [binding=weak, unwind=no] {
  block ^entry:
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    store i64 %value, @sink
    return i64 %value
}

function @main(%value : i64) -> i64 [role=entry] {
  block ^entry:
    %single_result = call i64 @single(%value)
    %multiple_left = call i64 @multiple(%single_result)
    %multiple_right = call i64 @multiple(%multiple_left)
    %address = addr @addressed
    %addressed_result = call i64 @addressed(%multiple_right)
    %indirect_result = call i64 %address(%addressed_result)
        as (%arg0 : i64) -> i64 [unwind=no]
    return i64 %indirect_result
}
