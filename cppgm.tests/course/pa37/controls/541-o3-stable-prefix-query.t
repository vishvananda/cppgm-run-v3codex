global @shared_values [binding=internal] = {
  i64 5
  i64 7
  i64 11
}
global @other_values [binding=internal] = {
  i64 13
  i64 17
  i64 19
}
global @second_values [binding=internal] = {
  i64 17
  i64 19
  i64 23
}
global @store_values [binding=internal] = {
  i64 5
  i64 7
  i64 11
}
global @call_values [binding=internal] = {
  i64 5
  i64 7
  i64 11
}

function @step(%value : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %next = binary add i64 %value, 1
    return i64 %next
}

function @prefix_query(%state : ptr, %index_value : i64) -> i64
    [binding=internal, query=stable_prefix] {
  block ^entry:
    %negative = cmp lt i64 %index_value, 0
    branch %negative, ^negative_result, ^read

  block ^negative_result:
    return i64 0

  block ^read:
    %offset = binary mul i64 %index_value, 8
    %address = index i8 %state, %offset
    %value = load i64 %address
    %zero = cmp eq i64 %index_value, 0
    branch %zero, ^return_zero, ^general

  block ^return_zero:
    return i64 %value

  block ^general:
    %v0 = call i64 @step(%value)
    %v1 = call i64 @step(%v0)
    %v2 = call i64 @step(%v1)
    %v3 = call i64 @step(%v2)
    %v4 = call i64 @step(%v3)
    %v5 = call i64 @step(%v4)
    %v6 = call i64 @step(%v5)
    %v7 = call i64 @step(%v6)
    %v8 = call i64 @step(%v7)
    %v9 = call i64 @step(%v8)
    %v10 = call i64 @step(%v9)
    %v11 = call i64 @step(%v10)
    %v12 = call i64 @step(%v11)
    %v13 = call i64 @step(%v12)
    %v14 = call i64 @step(%v13)
    %v15 = call i64 @step(%v14)
    %v16 = call i64 @step(%v15)
    %v17 = call i64 @step(%v16)
    %v18 = call i64 @step(%v17)
    %v19 = call i64 @step(%v18)
    %v20 = call i64 @step(%v19)
    %v21 = call i64 @step(%v20)
    %v22 = call i64 @step(%v21)
    %v23 = call i64 @step(%v22)
    %v24 = call i64 @step(%v23)
    %v25 = call i64 @step(%v24)
    %v26 = call i64 @step(%v25)
    %v27 = call i64 @step(%v26)
    %v28 = call i64 @step(%v27)
    %v29 = call i64 @step(%v28)
    %v30 = call i64 @step(%v29)
    %v31 = call i64 @step(%v30)
    %v32 = call i64 @step(%v31)
    %v33 = call i64 @step(%v32)
    %v34 = call i64 @step(%v33)
    %v35 = call i64 @step(%v34)
    %v36 = call i64 @step(%v35)
    %v37 = call i64 @step(%v36)
    %v38 = call i64 @step(%v37)
    %v39 = call i64 @step(%v38)
    %v40 = call i64 @step(%v39)
    %result = binary sub i64 %v40, 41
    return i64 %result
}

function @touch(%state : ptr) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 13, %state
    return void
}

function @clone_case0(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case1(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case2(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case3(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case4(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case5(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case6(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @clone_case7(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @descending_index(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 2)
    %lower = call i64 @prefix_query(%state, 0)
    %again = call i64 @prefix_query(%state, 2)
    %sum0 = binary add i64 %first, %lower
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @different_receiver(%first_state : ptr,
                             %second_state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%first_state, 0)
    %higher = call i64 @prefix_query(%second_state, 2)
    %again = call i64 @prefix_query(%first_state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @unknown_index(%state : ptr, %index_value : i64) -> i64
    [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %dynamic = call i64 @prefix_query(%state, %index_value)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %dynamic
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @negative_index(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %negative = call i64 @prefix_query(%state, -1)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %negative
    %sum1 = binary add i64 %sum0, %again
    return i64 %sum1
}

function @store_barrier(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    store i64 9, %state
    %again = call i64 @prefix_query(%state, 0)
    %sum = binary add i64 %first, %again
    return i64 %sum
}

function @call_barrier(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    call void @touch(%state)
    %again = call i64 @prefix_query(%state, 0)
    %sum = binary add i64 %first, %again
    return i64 %sum
}

function @equal_index(%state : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %first = call i64 @prefix_query(%state, 0)
    %again = call i64 @prefix_query(%state, 0)
    %sum = binary add i64 %first, %again
    return i64 %sum
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %shared = addr @shared_values
    %other = addr @other_values
    %second = addr @second_values
    %store = addr @store_values
    %called = addr @call_values
    %c0 = call i64 @clone_case0(%shared)
    %c1 = call i64 @clone_case1(%shared)
    %c2 = call i64 @clone_case2(%shared)
    %c3 = call i64 @clone_case3(%shared)
    %c4 = call i64 @clone_case4(%shared)
    %c5 = call i64 @clone_case5(%shared)
    %c6 = call i64 @clone_case6(%shared)
    %c7 = call i64 @clone_case7(%shared)
    %descending = call i64 @descending_index(%shared)
    %receivers = call i64 @different_receiver(%other, %second)
    %dynamic = call i64 @unknown_index(%shared, 1)
    %negative = call i64 @negative_index(%shared)
    %stored = call i64 @store_barrier(%store)
    %call = call i64 @call_barrier(%called)
    %equal = call i64 @equal_index(%shared)
    %sum0 = binary add i64 %c0, %c1
    %sum1 = binary add i64 %sum0, %c2
    %sum2 = binary add i64 %sum1, %c3
    %sum3 = binary add i64 %sum2, %c4
    %sum4 = binary add i64 %sum3, %c5
    %sum5 = binary add i64 %sum4, %c6
    %sum6 = binary add i64 %sum5, %c7
    %sum7 = binary add i64 %sum6, %descending
    %sum8 = binary add i64 %sum7, %receivers
    %sum9 = binary add i64 %sum8, %dynamic
    %sum10 = binary add i64 %sum9, %negative
    %sum11 = binary add i64 %sum10, %stored
    %sum12 = binary add i64 %sum11, %call
    %sum13 = binary add i64 %sum12, %equal
    %bad = cmp ne i64 %sum13, 313
    return i64 %bad
}
