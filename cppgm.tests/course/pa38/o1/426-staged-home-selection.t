function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @copy_to_edge_home(%value : i64, %condition : i64) -> i64 {
  block ^entry:
    eh_try ^handler
    %copied = copy i64 %value
    %ignored = call i64 @identity(7)
    eh_end
    branch %condition, ^left, ^right

  block ^left:
    %left_value = binary add i64 %copied, 1
    jump ^done

  block ^right:
    %right_value = binary sub i64 %copied, 1
    jump ^done

  block ^done:
    %result = phi i64 [^left: %left_value, ^right: %right_value]
    return i64 %result

  block ^handler:
    return i64 99
}

function @call_result_to_edge_home(%condition : i64) -> i64 {
  block ^entry:
    eh_try ^handler
    %value = call i64 @identity(40)
    %ignored = call i64 @identity(9)
    eh_end
    branch %condition, ^left, ^right

  block ^left:
    %left_value = binary add i64 %value, 2
    jump ^done

  block ^right:
    %right_value = binary sub i64 %value, 2
    jump ^done

  block ^done:
    %result = phi i64 [^left: %left_value, ^right: %right_value]
    return i64 %result

  block ^handler:
    return i64 99
}

global @keep1 : i64 = 1
global @keep2 : i64 = 2
global @keep3 : i64 = 3
global @keep4 : i64 = 4
global @keep5 : i64 = 5
global @number : i64 = 40
global @number_pointer = {
  ptr addr @number
}

function @load_with_dead_address(%condition : i64) -> i64 {
  block ^entry:
    eh_try ^handler
    %a = load i64 @keep1
    %b = load i64 @keep2
    %c = load i64 @keep3
    %d = load i64 @keep4
    %e = load i64 @keep5
    %address = load ptr @number_pointer
    %value = load i64 %address
    %ignored = call i64 @identity(11)
    eh_end
    branch %condition, ^left, ^right

  block ^left:
    %left0 = binary add i64 %a, %b
    %left1 = binary add i64 %c, %d
    %left2 = binary add i64 %left0, %left1
    %left3 = binary add i64 %left2, %e
    %left_value = binary add i64 %left3, 3
    jump ^done

  block ^right:
    %right0 = binary add i64 %a, %b
    %right1 = binary add i64 %c, %d
    %right2 = binary add i64 %right0, %right1
    %right3 = binary add i64 %right2, %e
    %right_value = binary sub i64 %right3, 3
    jump ^done

  block ^done:
    %result = phi i64 [^left: %left_value, ^right: %right_value]
    %sum = binary add i64 %result, %value
    return i64 %sum

  block ^handler:
    return i64 99
}

function @load_with_live_address() -> i64 {
  block ^entry:
    eh_try ^handler
    %address = load ptr @number_pointer
    %value = load i64 %address
    %ignored = call i64 @identity(13)
    eh_end
    %address_bad = cmp eq ptr %address, 0
    %value_bad = cmp ne i64 %value, 40
    %bad = binary or i64 %address_bad, %value_bad
    return i64 %bad

  block ^handler:
    return i64 99
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %copied = call i64 @copy_to_edge_home(40, 1)
    %called = call i64 @call_result_to_edge_home(1)
    %loaded = call i64 @load_with_dead_address(1)
    %live_address_bad = call i64 @load_with_live_address()
    %copy_bad = cmp ne i64 %copied, 41
    %call_bad = cmp ne i64 %called, 42
    %load_bad = cmp ne i64 %loaded, 58
    %partial = binary or i64 %copy_bad, %call_bad
    %partial2 = binary or i64 %load_bad, %live_address_bad
    %bad = binary or i64 %partial, %partial2
    return i64 %bad
}
