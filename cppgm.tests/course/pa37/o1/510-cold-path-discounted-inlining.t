declare function @observe(%value : i64) -> void
declare function @fail(%value : i64) -> void
    [return=noreturn]

function @cold_helper(%condition : i64, %value : i64) -> i64
    [binding=strong] {
  block ^entry:
    branch %condition, ^success, ^failure

  block ^success:
    return i64 %value

  block ^failure:
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @fail(%value)
    jump ^success
}

function @cold_a(%condition : i64, %value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @cold_helper(%condition, %value)
    return i64 %result
}

function @cold_b(%condition : i64, %value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @cold_helper(%condition, %value)
    return i64 %result
}

function @hot_helper(%condition : i64, %value : i64) -> i64
    [binding=strong] {
  block ^entry:
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    call void @observe(%value)
    branch %condition, ^success, ^failure

  block ^success:
    return i64 %value

  block ^failure:
    call void @fail(%value)
    jump ^success
}

function @hot_a(%condition : i64, %value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @hot_helper(%condition, %value)
    return i64 %result
}

function @hot_b(%condition : i64, %value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @hot_helper(%condition, %value)
    return i64 %result
}
