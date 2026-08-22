declare function @observe(%value : i32) -> void [unwind=no]

function @keeps_volatile_slot_accesses() -> i32 {
  slot $flag : i32

  block ^entry:
    store volatile i32 0, $flag
    store volatile i32 1, $flag
    %first = load volatile i32 $flag
    %second = load volatile i32 $flag
    %sum = binary add i32 %first, %second
    return i32 %sum
}

function @keeps_unused_volatile_load(%port : ptr) -> void {
  block ^entry:
    %discarded = load volatile i32 %port
    return void
}

function @keeps_ordinary_elimination(%port : ptr) -> i32 {
  slot $scratch : i32

  block ^entry:
    store i32 7, $scratch
    store i32 9, $scratch
    %kept = load i32 $scratch
    %repeat = load i32 $scratch
    %sum = binary add i32 %kept, %repeat
    return i32 %sum
}

function @main() -> i32 [role=entry] {
  slot $port : i32

  block ^entry:
    store i32 5, $port
    %port_address = addr $port
    call void @keeps_unused_volatile_load(%port_address)
    %volatile_sum = call i32 @keeps_volatile_slot_accesses()
    %ordinary_sum = call i32 @keeps_ordinary_elimination(%port_address)
    %total = binary add i32 %volatile_sum, %ordinary_sum
    %bad = cmp ne i32 %total, 20
    %wide = convert zext i32 u8 %bad
    return i32 %wide
}
