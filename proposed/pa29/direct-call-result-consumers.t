global @saved : i64 = 0

function @produce(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @consume(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @store_result(%value : i64) -> void {
  block ^entry:
    %result = call i64 @produce(%value)
    store i64 %result, @saved
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %produced = call i64 @produce(41)
    %consumed = call i64 @consume(%produced)
    call void @store_result(%consumed)
    %actual = load i64 @saved
    %bad = cmp ne i64 %actual, 41
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
