global @value : i64 = 3

declare function @readonly_may_throw() -> i64 [effects=readonly]
declare function @consume(%value : i64) -> void [unwind=no]

function @region_barriers() -> i64 {
  block ^entry:
    %before = load i64 @value
    eh_try ^cleanup
    %protected_first = load i64 @value
    %protected_second = load i64 @value
    %protected_sum = binary add i64 %protected_first, %protected_second
    %observed = call i64 @readonly_may_throw()
    %after_call_first = load i64 @value
    %after_call_second = load i64 @value
    %after_call_sum = binary add i64 %after_call_first, %after_call_second
    eh_end
    %after_region_first = load i64 @value
    %after_region_second = load i64 @value
    %after_region_sum = binary add i64 %after_region_first, %after_region_second
    %left = binary add i64 %before, %protected_sum
    %middle = binary add i64 %left, %observed
    %right = binary add i64 %after_call_sum, %after_region_sum
    %result = binary add i64 %middle, %right
    return i64 %result

  block ^cleanup:
    %landing_first = load i64 @value
    %landing_second = load i64 @value
    %landing_sum = binary add i64 %landing_first, %landing_second
    call void @consume(%landing_sum)
    resume
}

function @equal_protected_state(%choose : i64) -> i64 {
  block ^entry:
    eh_try ^cleanup
    %first = load i64 @value
    branch %choose, ^left, ^right

  block ^left:
    jump ^join

  block ^right:
    jump ^join

  block ^join:
    %second = load i64 @value
    %sum = binary add i64 %first, %second
    %observed = call i64 @readonly_may_throw()
    %result = binary add i64 %sum, %observed
    eh_end
    return i64 %result

  block ^cleanup:
    resume
}

function @conflicting_normal_and_handler_state() -> i64 {
  block ^entry:
    eh_try ^handler
    %normal = load i64 @value
    %observed = call i64 @readonly_may_throw()
    eh_end
    jump ^join

  block ^handler:
    %caught = load i64 @value
    jump ^join

  block ^join:
    %selected = phi i64 [^entry: %normal, ^handler: %caught]
    %joined = load i64 @value
    %result = binary add i64 %selected, %joined
    return i64 %result
}
