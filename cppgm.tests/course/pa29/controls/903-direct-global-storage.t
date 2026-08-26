global @value : i64 [binding=strong] = 41

function @direct_storage_use(%replacement : i64) -> i64 [unwind=no] {
  block ^entry:
    %address = addr @value
    %old = load i64 %address
    store i64 %replacement, %address
    return i64 %old
}

function @observe_address() -> ptr [unwind=no] {
  block ^entry:
    %address = addr @value
    return ptr %address
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %old = call i64 @direct_storage_use(42)
    %now = load i64 @value
    %observed = call ptr @observe_address()
    %old_bad = cmp ne i64 %old, 41
    %now_bad = cmp ne i64 %now, 42
    %address_bad = cmp eq ptr %observed, 0
    %partial = binary or i64 %old_bad, %now_bad
    %bad = binary or i64 %partial, %address_bad
    return i64 %bad
}
