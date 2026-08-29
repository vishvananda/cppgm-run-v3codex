global @positive_state [binding=internal] = {
  i64 0
  i64 0
}
global @cold_state [binding=internal] = {
  i64 0
  i64 0
  i64 0
}
global @store_state [binding=internal] = {
  i64 1
  i64 5
}
global @call_state [binding=internal] = {
  i64 1
  i64 5
}
global @different_state_a [binding=internal] = {
  i64 1
  i64 11
}
global @different_state_b [binding=internal] = {
  i64 1
  i64 13
}
global @volatile_state [binding=internal] = {
  i64 1
  i64 11
}

function @fail() -> void
    [binding=internal, return=noreturn, no_inline=yes] {
  block ^entry:
    unreachable
}

function @touch(%state : ptr) -> void
    [binding=internal, unwind=no, no_inline=yes] {
  block ^entry:
    %value_address = index i8 [projection=field] %state, 8
    store i64 9, %value_address
    return void
}

function @stable_query(%state : ptr) -> i64
    [binding=internal, no_inline=yes] {
  block ^entry:
    %ready_address = index i8 [projection=field] %state, 0
    %value_address = index i8 [projection=field] %state, 8
    jump ^guard

  block ^guard:
    %ready = load i64 %ready_address
    %empty = cmp eq i64 %ready, 0
    branch %empty, ^fill, ^return

  block ^fill:
    store i64 41, %value_address
    store i64 1, %ready_address
    jump ^guard

  block ^return:
    %value = load i64 %value_address
    return i64 %value
}

function @stable_query_with_cold_noreturn(%state : ptr) -> i64
    [binding=internal, no_inline=yes] {
  block ^entry:
    %ready_address = index i8 [projection=field] %state, 0
    %value_address = index i8 [projection=field] %state, 8
    %fail_address = index i8 [projection=field] %state, 16
    jump ^guard

  block ^guard:
    %ready = load i64 %ready_address
    %empty = cmp eq i64 %ready, 0
    branch %empty, ^slow, ^return

  block ^slow:
    %should_fail = load i64 %fail_address
    branch %should_fail, ^cold_failure, ^fill

  block ^fill:
    store i64 17, %value_address
    store i64 1, %ready_address
    jump ^guard

  block ^return:
    %value = load i64 %value_address
    return i64 %value

  block ^cold_failure:
    call void @fail()
    return i64 0
}

function @volatile_query(%state : ptr) -> i64
    [binding=internal, no_inline=yes] {
  block ^entry:
    %ready_address = index i8 [projection=field] %state, 0
    %value_address = index i8 [projection=field] %state, 8
    jump ^guard

  block ^guard:
    %ready = load i64 %ready_address
    %empty = cmp eq i64 %ready, 0
    branch %empty, ^fill, ^return

  block ^fill:
    store i64 11, %value_address
    store i64 1, %ready_address
    jump ^guard

  block ^return:
    %value = load volatile i64 %value_address
    return i64 %value
}

function @reuse_identical_query(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query(%state)
    %second = call i64 @stable_query(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @reuse_after_normal_cold_return(%state : ptr) -> i64
    [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query_with_cold_noreturn(%state)
    %second = call i64 @stable_query_with_cold_noreturn(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @keep_across_store(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query(%state)
    %value_address = index i8 [projection=field] %state, 8
    store i64 7, %value_address
    %second = call i64 @stable_query(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @keep_across_call(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query(%state)
    call void @touch(%state)
    %second = call i64 @stable_query(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @keep_different_arguments(%first_state : ptr,
                                   %second_state : ptr) -> i64
    [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query(%first_state)
    %second = call i64 @stable_query(%second_state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @keep_across_volatile(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @stable_query(%state)
    %observed = load volatile i64 %state
    %second = call i64 @stable_query(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @keep_volatile_query(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @volatile_query(%state)
    %second = call i64 @volatile_query(%state)
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %positive_address = addr @positive_state
    %positive = call i64 @reuse_identical_query(%positive_address)
    %cold_address = addr @cold_state
    %cold = call i64 @reuse_after_normal_cold_return(%cold_address)
    %store_address = addr @store_state
    %stored = call i64 @keep_across_store(%store_address)
    %call_address = addr @call_state
    %called = call i64 @keep_across_call(%call_address)
    %different_a = addr @different_state_a
    %different_b = addr @different_state_b
    %different = call i64 @keep_different_arguments(%different_a, %different_b)
    %volatile_address = addr @volatile_state
    %volatile_barrier = call i64 @keep_across_volatile(%volatile_address)
    %volatile_query = call i64 @keep_volatile_query(%volatile_address)
    %sum0 = binary add i64 %positive, %cold
    %sum1 = binary add i64 %sum0, %stored
    %sum2 = binary add i64 %sum1, %called
    %sum3 = binary add i64 %sum2, %different
    %sum4 = binary add i64 %sum3, %volatile_barrier
    %sum5 = binary add i64 %sum4, %volatile_query
    %bad = cmp ne i64 %sum5, 210
    return i64 %bad
}
