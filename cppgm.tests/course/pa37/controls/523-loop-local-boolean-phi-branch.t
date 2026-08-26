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
    branch %carried_ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
