global @observed : ptr = nullptr

function @observe(%value : ptr) -> void
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    store ptr %value, @observed
    return void
}

function @advance_slot(%cell : ptr) -> void [binding=internal, unwind=no] {
  slot $staging : ptr

  block ^entry:
    %current = load ptr %cell
    store ptr %current, $staging
    %stage01 = load ptr $staging
    store ptr %stage01, $staging
    %stage02 = load ptr $staging
    store ptr %stage02, $staging
    %stage03 = load ptr $staging
    store ptr %stage03, $staging
    %stage04 = load ptr $staging
    store ptr %stage04, $staging
    %stage05 = load ptr $staging
    store ptr %stage05, $staging
    %stage06 = load ptr $staging
    store ptr %stage06, $staging
    %stage07 = load ptr $staging
    store ptr %stage07, $staging
    %stage08 = load ptr $staging
    store ptr %stage08, $staging
    %stage09 = load ptr $staging
    store ptr %stage09, $staging
    %stage10 = load ptr $staging
    store ptr %stage10, $staging
    %stage11 = load ptr $staging
    store ptr %stage11, $staging
    %stage12 = load ptr $staging
    store ptr %stage12, $staging
    %stage13 = load ptr $staging
    store ptr %stage13, $staging
    %stage14 = load ptr $staging
    store ptr %stage14, $staging
    %stage15 = load ptr $staging
    store ptr %stage15, $staging
    %stage16 = load ptr $staging
    store ptr %stage16, $staging
    %stage17 = load ptr $staging
    store ptr %stage17, $staging
    %stage18 = load ptr $staging
    store ptr %stage18, $staging
    %stage19 = load ptr $staging
    store ptr %stage19, $staging
    %stage20 = load ptr $staging
    %next = index i8 %stage20, 8
    store ptr %next, %cell
    call void @observe(%next)
    call void @observe(%next)
    call void @observe(%next)
    call void @observe(%next)
    return void
}

function @keep_second_call(%cell : ptr) -> void
    [binding=internal, unwind=no] {
  block ^entry:
    call void @advance_slot(%cell)
    return void
}

function @walk(%base : ptr, %take_step : i64) -> ptr
    [binding=strong, no_inline=yes, unwind=no] {
  slot $result : ptr

  block ^entry:
    store ptr %base, $result
    branch %take_step, ^step, ^done

  block ^step:
    call void @advance_slot($result)
    jump ^done

  block ^done:
    %final = load ptr $result
    return ptr %final
}

function @main() -> i32 {
  slot $storage : obj<8x8>

  block ^entry:
    %base = addr $storage
    %result = call ptr @walk(%base, 1)
    %expected = index i8 %base, 8
    %ok = cmp eq ptr %result, %expected
    branch %ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
