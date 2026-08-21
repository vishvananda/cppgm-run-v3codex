global @value : i64 = 3

declare function @may_throw() -> void

function @atomic_barrier() -> i64 {
  block ^entry:
    %before = load i64 @value
    %address = addr @value
    %observed = atomic_load i64 %address, 5
    %after = load i64 @value
    %sum1 = binary add i64 %before, %observed
    %sum2 = binary add i64 %sum1, %after
    return i64 %sum2
}

function @exception_region() -> i64 {
  block ^entry:
    eh_try ^cleanup
    %before = load i64 @value
    %after = load i64 @value
    %sum = binary add i64 %before, %after
    call void @may_throw()
    eh_end
    return i64 %sum

  block ^cleanup:
    resume
}
