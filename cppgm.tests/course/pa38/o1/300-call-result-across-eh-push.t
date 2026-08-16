global @value : i64 = 0

function @address() -> ptr [unwind=no] {
  block ^entry:
    %p = addr @value
    return ptr %p
}

function @read(%p : ptr) -> i64 [unwind=no] {
  block ^entry:
    %value = load i64 %p
    return i64 %value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %p = call ptr @address()
    eh_try ^handler
    %result = call i64 @read(%p)
    eh_end
    %bad = cmp ne i64 %result, 0
    return i64 %bad

  block ^handler:
    return i64 2
}
