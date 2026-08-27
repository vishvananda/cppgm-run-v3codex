global @saved : i64 = 0

function @produce(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @consume(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @saved_address() -> ptr {
  block ^entry:
    %address = addr @saved
    return ptr %address
}

function @observe_reference(%value : ptr [pass=by_address]) -> void {
  block ^entry:
    return void
}

function @load_saved(%value : ptr) -> i64 {
  block ^entry:
    %loaded = load i64 %value
    return i64 %loaded
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
    %address = call ptr @saved_address()
    call void @observe_reference(%address)
    %loaded = call i64 @load_saved(%address)
    %bad = cmp ne i64 %actual, 41
    %bad_address = cmp ne i64 %loaded, 41
    %combined = binary or i64 %bad, %bad_address
    %exit = convert trunc i32 i64 %combined
    return i32 %exit
}
