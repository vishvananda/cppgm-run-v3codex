global @sink : i64 = 0

function @target(%value : i64) -> i64 [binding=weak, unwind=no] {
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

function @dead(%value : i64) -> i64 [binding=weak, unwind=no] {
  block ^entry:
    %result = call i64 @target(%value)
    return i64 %result
}

function @main(%value : i64) -> i64 [role=entry, unwind=no] {
  block ^entry:
    %result = call i64 @target(%value)
    return i64 %result
}
