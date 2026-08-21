declare function @may_throw(%value : i64) -> i64

function @wrapper(%value : i64) -> i64 [binding=weak] {
  block ^entry:
    %called = call i64 @may_throw(%value)
    %result = binary add i64 %called, 1
    return i64 %result
}

function @main(%value : i64) -> i64
    [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    eh_try ^dispatch
    %result = call i64 @wrapper(%value)
    eh_end
    return i64 %result

  block ^dispatch:
    resume
}
