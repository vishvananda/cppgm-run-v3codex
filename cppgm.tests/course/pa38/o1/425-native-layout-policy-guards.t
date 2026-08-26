function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @frameless_leaf(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @frameless_call(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = call i64 @identity(%value)
    return i64 %result
}

function @frame_operand() -> i64 [unwind=no] {
  slot $value : i64

  block ^entry:
    store i64 31, $value
    %result = load i64 $value
    return i64 %result
}

function @dynamic_stack(%bytes : i64) -> i64 [unwind=no] {
  block ^entry:
    %space = stack_alloc %bytes
    store i64 37, %space
    %result = load i64 %space
    return i64 %result
}

function @floating_scratch() -> i64 [unwind=no] {
  block ^entry:
    %sum = binary add f64 1.25, 2.5
    %bad = cmp ne f64 %sum, 3.75
    return i64 %bad
}

function @host_eh() -> i64 {
  block ^entry:
    eh_try ^handler
    %value = call i64 @identity(41)
    eh_end
    return i64 %value

  block ^handler:
    return i64 99
}

function @direct_returns(%condition : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %condition, ^yes, ^no

  block ^yes:
    return i64 43

  block ^no:
    return i64 47
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %leaf = call i64 @frameless_leaf(28)
    %called = call i64 @frameless_call(30)
    %framed = call i64 @frame_operand()
    %dynamic = call i64 @dynamic_stack(16)
    %floating_bad = call i64 @floating_scratch()
    %eh_value = call i64 @host_eh()
    %returned = call i64 @direct_returns(1)
    %leaf_bad = cmp ne i64 %leaf, 29
    %call_bad = cmp ne i64 %called, 30
    %frame_bad = cmp ne i64 %framed, 31
    %dynamic_bad = cmp ne i64 %dynamic, 37
    %eh_bad = cmp ne i64 %eh_value, 41
    %return_bad = cmp ne i64 %returned, 43
    %bad0 = binary or i64 %leaf_bad, %call_bad
    %bad1 = binary or i64 %frame_bad, %dynamic_bad
    %bad2 = binary or i64 %floating_bad, %eh_bad
    %bad3 = binary or i64 %bad0, %bad1
    %bad4 = binary or i64 %bad2, %return_bad
    %bad = binary or i64 %bad3, %bad4
    return i64 %bad
}
