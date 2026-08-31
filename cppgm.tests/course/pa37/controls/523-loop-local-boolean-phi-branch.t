global @observed : i64 = 0

function @observe(%value : i64) -> void [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 %value, @observed
    return void
}

function @conditionally_throw(%value : i64) -> void [binding=internal, no_inline=yes] {
  block ^entry:
    %nonzero = cmp ne i64 %value, 0
    branch %nonzero, ^done, ^raise

  block ^raise:
    throw i64 101

  block ^done:
    return void
}

function @fold_loop_local_choice(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %remaining = phi i64 [^entry: %count, ^hit: %hit_next, ^miss: %miss_next]
    %positive = cmp gt i64 %remaining, 0
    branch %positive, ^choose, ^done

  block ^choose:
    %low_bit = binary and i64 %remaining, 1
    %even = cmp eq i64 %low_bit, 0
    branch %even, ^rhs, ^short

  block ^rhs:
    %more_than_one = cmp gt i64 %remaining, 1
    jump ^choice

  block ^short:
    jump ^choice

  block ^choice:
    %take_pair = phi i64 [^rhs: %more_than_one, ^short: 0]
    branch %take_pair, ^hit, ^miss

  block ^hit:
    %hit_next = binary sub i64 %remaining, 2
    jump ^loop

  block ^miss:
    %miss_next = binary sub i64 %remaining, 1
    jump ^loop

  block ^done:
    return i64 %remaining
}

function @retain_shared_choice(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %remaining = phi i64 [^entry: %count, ^hit: %hit_next, ^miss: %miss_next]
    %positive = cmp gt i64 %remaining, 0
    branch %positive, ^choose, ^done

  block ^choose:
    %low_bit = binary and i64 %remaining, 1
    %even = cmp eq i64 %low_bit, 0
    branch %even, ^rhs, ^short

  block ^rhs:
    %more_than_one = cmp gt i64 %remaining, 1
    jump ^choice

  block ^short:
    jump ^choice

  block ^choice:
    %take_pair = phi i64 [^rhs: %more_than_one, ^short: 0]
    call void @observe(%take_pair)
    branch %take_pair, ^hit, ^miss

  block ^hit:
    %hit_next = binary sub i64 %remaining, 2
    jump ^loop

  block ^miss:
    %miss_next = binary sub i64 %remaining, 1
    jump ^loop

  block ^done:
    return i64 %remaining
}

function @retain_loop_carried_choice(%initial : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %choice = phi i64 [^entry: %initial, ^back: 0]
    branch %choice, ^back, ^done

  block ^back:
    jump ^loop

  block ^done:
    return i64 7
}

function @fold_acyclic_boolean_diamond(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    branch %positive, ^positive_arm, ^nonpositive_arm

  block ^positive_arm:
    jump ^choice

  block ^nonpositive_arm:
    jump ^choice

  block ^choice:
    %choose_first = phi u8 [^positive_arm: 0, ^nonpositive_arm: 1]
    branch %choose_first, ^first, ^second

  block ^first:
    return i64 11

  block ^second:
    return i64 22
}

function @retain_effectful_acyclic_diamond(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    branch %positive, ^positive_arm, ^nonpositive_arm

  block ^positive_arm:
    call void @observe(41)
    jump ^choice

  block ^nonpositive_arm:
    jump ^choice

  block ^choice:
    %choose_first = phi u8 [^positive_arm: 1, ^nonpositive_arm: 0]
    branch %choose_first, ^first, ^second

  block ^first:
    return i64 31

  block ^second:
    return i64 37
}

function @retain_shared_acyclic_diamond(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    branch %positive, ^positive_arm, ^nonpositive_arm

  block ^positive_arm:
    jump ^choice

  block ^nonpositive_arm:
    jump ^choice

  block ^choice:
    %choose_first = phi u8 [^positive_arm: 1, ^nonpositive_arm: 0]
    %choice_value = convert zext i64 u8 %choose_first
    call void @observe(%choice_value)
    branch %choose_first, ^first, ^second

  block ^first:
    return i64 43

  block ^second:
    return i64 47
}

function @retain_non_boolean_acyclic_diamond(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    branch %positive, ^positive_arm, ^nonpositive_arm

  block ^positive_arm:
    jump ^choice

  block ^nonpositive_arm:
    jump ^choice

  block ^choice:
    %choose_first = phi u8 [^positive_arm: 0, ^nonpositive_arm: 2]
    branch %choose_first, ^first, ^second

  block ^first:
    return i64 53

  block ^second:
    return i64 59
}

function @fold_forwarded_boolean(%value : i64, %choose_rhs : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %use_rhs = cmp ne i64 %choose_rhs, 0
    branch %use_rhs, ^rhs, ^short

  block ^rhs:
    call void @observe(73)
    %positive = cmp gt i64 %value, 0
    jump ^wide_merge

  block ^short:
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^rhs: %positive, ^short: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    branch %narrow_choice, ^hit, ^miss

  block ^hit:
    return i64 1

  block ^miss:
    return i64 0
}

function @retain_shared_forwarded_boolean(%value : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %use_rhs = cmp ne i64 %value, 0
    branch %use_rhs, ^rhs, ^short

  block ^rhs:
    %positive = cmp gt i64 %value, 0
    jump ^wide_merge

  block ^short:
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^rhs: %positive, ^short: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    %observed_choice = convert zext i64 u8 %narrow_choice
    call void @observe(%observed_choice)
    branch %narrow_choice, ^hit, ^miss

  block ^hit:
    return i64 1

  block ^miss:
    return i64 0
}

function @retain_non_boolean_forwarded(%value : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %use_value = cmp ne i64 %value, 0
    branch %use_value, ^rhs, ^short

  block ^rhs:
    jump ^wide_merge

  block ^short:
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^rhs: %value, ^short: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    branch %narrow_choice, ^hit, ^miss

  block ^hit:
    return i64 1

  block ^miss:
    return i64 0
}

function @retain_successor_phi(%value : i64, %alternate : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %take_alternate = cmp ne i64 %alternate, 0
    branch %take_alternate, ^alternate_hit, ^choose

  block ^alternate_hit:
    jump ^hit

  block ^choose:
    %use_rhs = cmp ne i64 %value, 0
    branch %use_rhs, ^rhs, ^short

  block ^rhs:
    %positive = cmp gt i64 %value, 0
    jump ^wide_merge

  block ^short:
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^rhs: %positive, ^short: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    branch %narrow_choice, ^hit, ^miss

  block ^hit:
    %answer = phi i64 [^alternate_hit: 2, ^decision: 1]
    return i64 %answer

  block ^miss:
    return i64 0
}

function @retain_cyclic_forwarded_boolean(%value : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^entry: %positive, ^loopback: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    branch %narrow_choice, ^loopback, ^done

  block ^loopback:
    jump ^wide_merge

  block ^done:
    return i64 83
}

function @retain_eh_forwarded_boolean(%value : i64) -> i64 [no_inline=yes] {
  block ^entry:
    eh_try ^cleanup
    %use_rhs = cmp ne i64 %value, 0
    branch %use_rhs, ^rhs, ^short

  block ^rhs:
    call void @conditionally_throw(%value)
    %positive = cmp gt i64 %value, 0
    jump ^wide_merge

  block ^short:
    jump ^wide_merge

  block ^wide_merge:
    %wide_choice = phi i64 [^rhs: %positive, ^short: 0]
    %narrow_choice = convert trunc u8 i64 %wide_choice
    jump ^decision

  block ^decision:
    branch %narrow_choice, ^hit, ^miss

  block ^hit:
    eh_end
    return i64 89

  block ^miss:
    eh_end
    return i64 97

  block ^cleanup:
    resume
}

function @main() -> i32 {
  block ^entry:
    %folded = call i64 @fold_loop_local_choice(5)
    %folded_ok = cmp eq i64 %folded, 0
    branch %folded_ok, ^shared, ^fail

  block ^shared:
    %retained = call i64 @retain_shared_choice(5)
    %retained_ok = cmp eq i64 %retained, 0
    branch %retained_ok, ^carried, ^fail

  block ^carried:
    %carried_result = call i64 @retain_loop_carried_choice(1)
    %carried_ok = cmp eq i64 %carried_result, 7
    branch %carried_ok, ^acyclic_positive, ^fail

  block ^acyclic_positive:
    %positive_choice = call i64 @fold_acyclic_boolean_diamond(5)
    %positive_choice_ok = cmp eq i64 %positive_choice, 22
    branch %positive_choice_ok, ^acyclic_negative, ^fail

  block ^acyclic_negative:
    %negative_choice = call i64 @fold_acyclic_boolean_diamond(-5)
    %negative_choice_ok = cmp eq i64 %negative_choice, 11
    branch %negative_choice_ok, ^effectful, ^fail

  block ^effectful:
    %effectful_result = call i64 @retain_effectful_acyclic_diamond(5)
    %effectful_result_ok = cmp eq i64 %effectful_result, 31
    %effectful_value = load i64 @observed
    %effectful_value_ok = cmp eq i64 %effectful_value, 41
    %effectful_ok = binary and i64 %effectful_result_ok, %effectful_value_ok
    branch %effectful_ok, ^effectful_skipped, ^fail

  block ^effectful_skipped:
    %skipped_result = call i64 @retain_effectful_acyclic_diamond(-5)
    %skipped_result_ok = cmp eq i64 %skipped_result, 37
    %skipped_value = load i64 @observed
    %skipped_value_ok = cmp eq i64 %skipped_value, 41
    %skipped_ok = binary and i64 %skipped_result_ok, %skipped_value_ok
    branch %skipped_ok, ^shared_acyclic, ^fail

  block ^shared_acyclic:
    %shared_result = call i64 @retain_shared_acyclic_diamond(-5)
    %shared_result_ok = cmp eq i64 %shared_result, 47
    %shared_value = load i64 @observed
    %shared_value_ok = cmp eq i64 %shared_value, 0
    %shared_ok = binary and i64 %shared_result_ok, %shared_value_ok
    branch %shared_ok, ^non_boolean, ^fail

  block ^non_boolean:
    %non_boolean_result = call i64 @retain_non_boolean_acyclic_diamond(-5)
    %non_boolean_ok = cmp eq i64 %non_boolean_result, 53
    branch %non_boolean_ok, ^non_boolean_zero, ^fail

  block ^non_boolean_zero:
    %non_boolean_zero_result = call i64 @retain_non_boolean_acyclic_diamond(5)
    %non_boolean_zero_ok = cmp eq i64 %non_boolean_zero_result, 59
    branch %non_boolean_zero_ok, ^forwarded, ^fail

  block ^forwarded:
    store i64 0, @observed
    %forwarded_result = call i64 @fold_forwarded_boolean(5, 1)
    %forwarded_result_ok = cmp eq i64 %forwarded_result, 1
    %forwarded_observed = load i64 @observed
    %forwarded_observed_ok = cmp eq i64 %forwarded_observed, 73
    %forwarded_ok = binary and i64 %forwarded_result_ok, %forwarded_observed_ok
    branch %forwarded_ok, ^forwarded_short, ^fail

  block ^forwarded_short:
    %forwarded_short_result = call i64 @fold_forwarded_boolean(5, 0)
    %forwarded_short_ok = cmp eq i64 %forwarded_short_result, 0
    %forwarded_short_observed = load i64 @observed
    %forwarded_short_observed_ok = cmp eq i64 %forwarded_short_observed, 73
    %forwarded_short_all_ok = binary and i64 %forwarded_short_ok, %forwarded_short_observed_ok
    branch %forwarded_short_all_ok, ^shared_forwarded, ^fail

  block ^shared_forwarded:
    %shared_forwarded_result = call i64 @retain_shared_forwarded_boolean(5)
    %shared_forwarded_ok = cmp eq i64 %shared_forwarded_result, 1
    branch %shared_forwarded_ok, ^non_boolean_forwarded, ^fail

  block ^non_boolean_forwarded:
    %non_boolean_forwarded_result = call i64 @retain_non_boolean_forwarded(256)
    %non_boolean_forwarded_ok = cmp eq i64 %non_boolean_forwarded_result, 0
    branch %non_boolean_forwarded_ok, ^successor_phi, ^fail

  block ^successor_phi:
    %ordinary_phi_result = call i64 @retain_successor_phi(5, 0)
    %ordinary_phi_ok = cmp eq i64 %ordinary_phi_result, 1
    %alternate_phi_result = call i64 @retain_successor_phi(5, 1)
    %alternate_phi_ok = cmp eq i64 %alternate_phi_result, 2
    %successor_phi_ok = binary and i64 %ordinary_phi_ok, %alternate_phi_ok
    branch %successor_phi_ok, ^cyclic_forwarded, ^fail

  block ^cyclic_forwarded:
    %cyclic_forwarded_result = call i64 @retain_cyclic_forwarded_boolean(5)
    %cyclic_forwarded_ok = cmp eq i64 %cyclic_forwarded_result, 83
    branch %cyclic_forwarded_ok, ^eh_forwarded, ^fail

  block ^eh_forwarded:
    %eh_forwarded_result = call i64 @retain_eh_forwarded_boolean(5)
    %eh_forwarded_ok = cmp eq i64 %eh_forwarded_result, 89
    branch %eh_forwarded_ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
