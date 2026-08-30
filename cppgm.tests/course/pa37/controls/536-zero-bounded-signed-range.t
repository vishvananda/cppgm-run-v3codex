function @fold_zero_bounded(%value : i32) -> i32 [no_inline=yes, unwind=no] {
  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    return i32 1

  block ^reject:
    return i32 0
}

function @fold_with_accept_phi(%value : i32, %alternate : i64) -> i32 [no_inline=yes, unwind=no] {
  block ^entry:
    branch %alternate, ^alternate_accept, ^lower

  block ^alternate_accept:
    jump ^accept

  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    %answer = phi i32 [^alternate_accept: 2, ^upper: 1]
    return i32 %answer

  block ^reject:
    return i32 0
}

function @retain_distinct_rejections(%value : i32) -> i32 [no_inline=yes, unwind=no] {
  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^negative, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^too_large, ^accept

  block ^negative:
    return i32 11

  block ^too_large:
    return i32 13

  block ^accept:
    return i32 17
}

function @retain_distinct_values(%lower_value : i32, %upper_value : i32) -> i32 [no_inline=yes, unwind=no] {
  block ^lower:
    %negative = cmp lt i32 %lower_value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %upper_value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    return i32 1

  block ^reject:
    return i32 0
}

function @retain_shared_upper(%value : i32, %bypass : i64) -> i32 [no_inline=yes, unwind=no] {
  block ^entry:
    branch %bypass, ^upper, ^lower

  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    return i32 1

  block ^reject:
    return i32 0
}

function @retain_shared_upper_predicate(%value : i32) -> i64 [no_inline=yes, unwind=no] {
  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    return i64 %too_large

  block ^reject:
    return i64 1
}

function @retain_rejection_phi(%value : i32, %alternate : i64) -> i32 [no_inline=yes, unwind=no] {
  block ^entry:
    branch %alternate, ^alternate_reject, ^lower

  block ^alternate_reject:
    jump ^reject

  block ^lower:
    %negative = cmp lt i32 %value, 0
    branch %negative, ^reject, ^upper

  block ^upper:
    %too_large = cmp gt i32 %value, 127
    branch %too_large, ^reject, ^accept

  block ^accept:
    return i32 19

  block ^reject:
    %answer = phi i32 [^alternate_reject: 17, ^lower: 11, ^upper: 13]
    return i32 %answer
}

function @main() -> i32 {
  block ^entry:
    %below = call i32 @fold_zero_bounded(-1)
    %below_ok = cmp eq i32 %below, 0
    branch %below_ok, ^lower_edge, ^fail

  block ^lower_edge:
    %lower = call i32 @fold_zero_bounded(0)
    %lower_ok = cmp eq i32 %lower, 1
    branch %lower_ok, ^upper_edge, ^fail

  block ^upper_edge:
    %upper = call i32 @fold_zero_bounded(127)
    %upper_ok = cmp eq i32 %upper, 1
    branch %upper_ok, ^above, ^fail

  block ^above:
    %high = call i32 @fold_zero_bounded(128)
    %high_ok = cmp eq i32 %high, 0
    branch %high_ok, ^accept_phi, ^fail

  block ^accept_phi:
    %ordinary_accept = call i32 @fold_with_accept_phi(64, 0)
    %ordinary_accept_ok = cmp eq i32 %ordinary_accept, 1
    %alternate_accept = call i32 @fold_with_accept_phi(-1, 1)
    %alternate_accept_ok = cmp eq i32 %alternate_accept, 2
    %both_accept_ok = binary and i64 %ordinary_accept_ok, %alternate_accept_ok
    branch %both_accept_ok, ^distinct, ^fail

  block ^distinct:
    %negative_result = call i32 @retain_distinct_rejections(-1)
    %negative_ok = cmp eq i32 %negative_result, 11
    %large_result = call i32 @retain_distinct_rejections(128)
    %large_ok = cmp eq i32 %large_result, 13
    %distinct_ok = binary and i64 %negative_ok, %large_ok
    branch %distinct_ok, ^distinct_values, ^fail

  block ^distinct_values:
    %distinct_value_result = call i32 @retain_distinct_values(1, 200)
    %distinct_value_ok = cmp eq i32 %distinct_value_result, 0
    branch %distinct_value_ok, ^shared, ^fail

  block ^shared:
    %bypassed = call i32 @retain_shared_upper(-1, 1)
    %bypassed_ok = cmp eq i32 %bypassed, 1
    %checked = call i32 @retain_shared_upper(-1, 0)
    %checked_ok = cmp eq i32 %checked, 0
    %shared_ok = binary and i64 %bypassed_ok, %checked_ok
    branch %shared_ok, ^shared_predicate, ^fail

  block ^shared_predicate:
    %shared_predicate_result = call i64 @retain_shared_upper_predicate(64)
    %shared_predicate_ok = cmp eq i64 %shared_predicate_result, 0
    branch %shared_predicate_ok, ^reject_phi, ^fail

  block ^reject_phi:
    %negative_phi = call i32 @retain_rejection_phi(-1, 0)
    %negative_phi_ok = cmp eq i32 %negative_phi, 11
    %large_phi = call i32 @retain_rejection_phi(128, 0)
    %large_phi_ok = cmp eq i32 %large_phi, 13
    %phi_values_ok = binary and i64 %negative_phi_ok, %large_phi_ok
    %alternate_phi = call i32 @retain_rejection_phi(64, 1)
    %alternate_phi_ok = cmp eq i32 %alternate_phi, 17
    %all_phi_ok = binary and i64 %phi_values_ok, %alternate_phi_ok
    branch %all_phi_ok, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}
