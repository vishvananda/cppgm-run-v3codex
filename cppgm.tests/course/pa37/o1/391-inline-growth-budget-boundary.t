global @sink : i64 = 0

function @piece(%value : i64) -> void [binding=strong, unwind=no] {
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
    return void
}

function @driver(%value : i64) -> void [binding=strong] {
  block ^entry:
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    call void @piece(%value)
    return void
}
