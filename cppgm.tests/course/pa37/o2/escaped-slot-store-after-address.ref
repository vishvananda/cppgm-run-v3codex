declare function @observe(%value : ptr) -> void [effects=readwrite, unwind=no]

function @preserve_escaped_stores() -> void {
  slot $value : i64

  block ^entry:
    store i64 1, $value
    %address = addr $value
    call void @observe(%address)
    store i64 8, $value
    call void @observe(%address)
    return void
}
