global @observed : i64 = 0

function @observe(%value : i64) -> void [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 %value, @observed
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
    branch %non_boolean_zero_ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
