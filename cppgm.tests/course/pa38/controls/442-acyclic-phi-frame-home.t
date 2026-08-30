global @observed : i64 = 0
global @phi_input : i64 = 7
global @phi_zero_a : i64 = 0
global @phi_zero_b : i64 = 0
global @transfer_observed : i64 = 0

function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @single_use_chain(%outer : i64, %inner : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %outer, ^outer_value, ^inner_head

  block ^outer_value:
    jump ^join

  block ^inner_head:
    branch %inner, ^inner_true, ^inner_false

  block ^inner_true:
    jump ^inner_join

  block ^inner_false:
    jump ^inner_join

  block ^inner_join:
    %inner_value = phi i64 [^inner_true: 11, ^inner_false: 22]
    jump ^join

  block ^join:
    %result = phi i64 [^outer_value: 33, ^inner_join: %inner_value]
    return i64 %result
}

function @multi_use_guard(%outer : i64, %inner : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %inner, ^inner_true, ^inner_false

  block ^inner_true:
    jump ^inner_join

  block ^inner_false:
    jump ^inner_join

  block ^inner_join:
    %inner_value = phi i64 [^inner_true: 44, ^inner_false: 55]
    branch %outer, ^outer_value, ^inner_value_edge

  block ^outer_value:
    jump ^join

  block ^inner_value_edge:
    jump ^join

  block ^join:
    %result = phi i64 [^outer_value: 66, ^inner_value_edge: %inner_value]
    store i64 %inner_value, @observed
    return i64 %result
}

function @last_transfer_chain(%outer : i64, %inner : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %inner, ^inner_true, ^inner_false

  block ^inner_true:
    jump ^inner_join

  block ^inner_false:
    jump ^inner_join

  block ^inner_join:
    %inner_value = phi i64 [^inner_true: 11, ^inner_false: 22]
    %observed = binary add i64 %inner_value, 1
    store i64 %observed, @transfer_observed
    branch %outer, ^outer_value, ^inner_value_edge

  block ^outer_value:
    jump ^join

  block ^inner_value_edge:
    jump ^join

  block ^join:
    %result = phi i64 [^outer_value: 33, ^inner_value_edge: %inner_value]
    return i64 %result
}

function @loop_carried_guard(%seed_choice : i64, %count : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %seed_choice, ^seed_true, ^seed_false

  block ^seed_true:
    jump ^seed_join

  block ^seed_false:
    jump ^seed_join

  block ^seed_join:
    %seed = phi i64 [^seed_true: 7, ^seed_false: 9]
    jump ^loop

  block ^loop:
    %value = phi i64 [^seed_join: %seed, ^body: %next]
    %again = cmp ult i64 %value, %count
    branch %again, ^body, ^done

  block ^body:
    %next = binary add i64 %value, 1
    jump ^loop

  block ^done:
    return i64 %value
}

function @repeated_merge_guard(%seed_choice : i64, %count : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %seed_choice, ^seed_true, ^seed_false

  block ^seed_true:
    jump ^seed_join

  block ^seed_false:
    jump ^seed_join

  block ^seed_join:
    %seed = phi i64 [^seed_true: 7, ^seed_false: 9]
    jump ^loop

  block ^loop:
    %index = phi i64 [^seed_join: 0, ^choice_join: %next]
    %sum = phi i64 [^seed_join: 0, ^choice_join: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^body

  block ^body:
    %use_seed = cmp ne i64 %index, 0
    branch %use_seed, ^seed_value, ^zero_value

  block ^seed_value:
    jump ^choice_join

  block ^zero_value:
    jump ^choice_join

  block ^choice_join:
    %choice = phi i64 [^seed_value: %seed, ^zero_value: 0]
    %next_sum = binary add i64 %sum, %choice
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    return i64 %sum
}

function @repeated_local_chain(%inner : i64, %count : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^choice_join: %next]
    %sum = phi i64 [^entry: 0, ^choice_join: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^body

  block ^body:
    branch %inner, ^inner_true, ^inner_false

  block ^inner_true:
    jump ^inner_join

  block ^inner_false:
    jump ^inner_join

  block ^inner_join:
    %inner_value = phi i64 [^inner_true: 11, ^inner_false: 22]
    %use_inner = cmp ne i64 %index, 0
    branch %use_inner, ^inner_value_edge, ^outer_value

  block ^inner_value_edge:
    jump ^choice_join

  block ^outer_value:
    jump ^choice_join

  block ^choice_join:
    %choice = phi i64 [^inner_value_edge: %inner_value, ^outer_value: 33]
    %next_sum = binary add i64 %sum, %choice
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    return i64 %sum
}

function @immediate_call_phi_home(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    %bias_a = load i64 @phi_zero_a
    %bias_b = load i64 @phi_zero_b
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^join: %next]
    %sum = phi i64 [^entry: 0, ^join: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^body

  block ^body:
    %odd = binary and i64 %index, 1
    branch %odd, ^right, ^left

  block ^left:
    %left_value = load i64 @phi_input
    %noise = call i64 @identity(%index)
    jump ^join

  block ^right:
    %right_value = binary add i64 %index, 20
    jump ^join

  block ^join:
    %choice = phi i64 [^left: %left_value, ^right: %right_value]
    %used = call i64 @identity(%choice)
    %next_sum = binary add i64 %sum, %used
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    %with_a = binary add i64 %sum, %bias_a
    %with_b = binary add i64 %with_a, %bias_b
    return i64 %with_b
}

function @non_immediate_call_phi_home(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    %bias_a = load i64 @phi_zero_a
    %bias_b = load i64 @phi_zero_b
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^join: %next]
    %sum = phi i64 [^entry: 0, ^join: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^body

  block ^body:
    %odd = binary and i64 %index, 1
    branch %odd, ^right, ^left

  block ^left:
    %left_value = load i64 @phi_input
    %noise = call i64 @identity(%index)
    jump ^join

  block ^right:
    %right_value = binary add i64 %index, 20
    jump ^join

  block ^join:
    %choice = phi i64 [^left: %left_value, ^right: %right_value]
    %join_noise = call i64 @identity(%index)
    %used = call i64 @identity(%choice)
    %next_sum = binary add i64 %sum, %used
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    %with_a = binary add i64 %sum, %bias_a
    %with_b = binary add i64 %with_a, %bias_b
    return i64 %with_b
}

function @repeated_invariant_call_guard(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    %bias_a = load i64 @phi_zero_a
    %bias_b = load i64 @phi_zero_b
    %stable = load i64 @phi_input
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^join: %next]
    %sum = phi i64 [^entry: 0, ^join: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^body

  block ^body:
    %use_stable = cmp ne i64 %index, 0
    branch %use_stable, ^stable_edge, ^fresh_edge

  block ^stable_edge:
    jump ^join

  block ^fresh_edge:
    %fresh = call i64 @identity(99)
    jump ^join

  block ^join:
    %choice = phi i64 [^stable_edge: %stable, ^fresh_edge: %fresh]
    %used = call i64 @identity(%choice)
    %next_sum = binary add i64 %sum, %used
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    %with_a = binary add i64 %sum, %bias_a
    %with_b = binary add i64 %with_a, %bias_b
    return i64 %with_b
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %inner = call i64 @single_use_chain(0, 1)
    %outer = call i64 @single_use_chain(1, 0)
    %multi = call i64 @multi_use_guard(1, 1)
    %last_transfer = call i64 @last_transfer_chain(0, 1)
    %transfer_saved = load i64 @transfer_observed
    %saved = load i64 @observed
    %looped = call i64 @loop_carried_guard(1, 10)
    %repeated = call i64 @repeated_merge_guard(1, 2)
    %local = call i64 @repeated_local_chain(1, 2)
    %call_phi = call i64 @immediate_call_phi_home(2)
    %delayed_phi = call i64 @non_immediate_call_phi_home(2)
    %guarded = call i64 @repeated_invariant_call_guard(2)
    %bad0 = cmp ne i64 %inner, 11
    %bad1 = cmp ne i64 %outer, 33
    %bad2 = cmp ne i64 %multi, 66
    %bad3 = cmp ne i64 %saved, 44
    %bad18 = cmp ne i64 %last_transfer, 11
    %bad19 = cmp ne i64 %transfer_saved, 12
    %bad4 = cmp ne i64 %looped, 10
    %bad8 = cmp ne i64 %repeated, 7
    %bad10 = cmp ne i64 %local, 44
    %bad12 = cmp ne i64 %call_phi, 28
    %bad16 = cmp ne i64 %delayed_phi, 28
    %bad13 = cmp ne i64 %guarded, 106
    %bad5 = binary or i64 %bad0, %bad1
    %bad6 = binary or i64 %bad2, %bad3
    %bad7 = binary or i64 %bad4, %bad5
    %bad9 = binary or i64 %bad6, %bad7
    %bad11 = binary or i64 %bad8, %bad9
    %bad14 = binary or i64 %bad10, %bad11
    %bad15 = binary or i64 %bad12, %bad13
    %bad17 = binary or i64 %bad15, %bad16
    %bad20 = binary or i64 %bad18, %bad19
    %bad21 = binary or i64 %bad14, %bad17
    %bad = binary or i64 %bad20, %bad21
    return i64 %bad
}
