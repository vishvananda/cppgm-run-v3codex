declare function @unknown_effects() -> void

global @stable : i64 = 7
global @changed : i64 = 1
global @other : i64 = 0

function @hoist_disjoint_global(%limit : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %value = load i64 @stable
    store i64 %value, @other
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

function @keep_aliased_global(%limit : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %value = load i64 @changed
    store i64 %value, @changed
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

function @keep_across_unknown_call(%limit : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %value = load i64 @stable
    call void @unknown_effects()
    store i64 %value, @other
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

function @keep_across_atomic(%limit : i64) -> i64 [no_inline=yes] {
  block ^entry:
    %address = addr @stable
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %value = load i64 @stable
    atomic_store i64 %i, %address, 0
    store i64 %value, @other
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}
