declare function @observe(%value : i64) -> void [unwind=no]

function @wrapper(%value : i64) -> void [unwind=no] {
  block ^entry:
    call void @observe(%value)
    return void
}

function @caller(%condition : i64) -> i64 [binding=strong] {
  block ^entry:
    branch %condition, ^with_call, ^other

  block ^with_call:
    call void @wrapper(7)
    %after = binary add i64 10, 1
    jump ^join

  block ^other:
    jump ^join

  block ^join:
    %result = phi i64 [^with_call: %after, ^other: 22]
    return i64 %result
}
