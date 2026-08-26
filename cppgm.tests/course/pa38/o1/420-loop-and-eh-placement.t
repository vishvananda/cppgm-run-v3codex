global @node1 = {
  ptr addr @node2
}
global @node2 = {
  ptr 0
}
global @number : i64 = 42

function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @walk_unavoidable(%head : ptr) -> i64 [unwind=no] {
  block ^entry:
    %limit = load i64 @number
    jump ^loop

  block ^loop:
    %cursor = phi ptr [^entry: %head, ^advance: %next]
    %count = phi i64 [^entry: 0, ^advance: %next_count]
    %done = cmp eq ptr %cursor, 0
    branch %done, ^exit, ^body

  block ^body:
    %limit_bad = cmp ne i64 %limit, 42
    branch %limit_bad, ^failure, ^advance

  block ^advance:
    %next = load ptr %cursor
    %next_count = binary add i64 %count, 1
    jump ^loop

  block ^exit:
    return i64 %count

  block ^failure:
    return i64 99
}

function @walk_guarded(%head : ptr, %run : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %run, ^loop, ^exit

  block ^loop:
    %cursor = phi ptr [^entry: %head, ^body: %next]
    %count = phi i64 [^entry: 0, ^body: %next_count]
    %done = cmp eq ptr %cursor, 0
    branch %done, ^exit, ^body

  block ^body:
    %next = load ptr %cursor
    %next_count = binary add i64 %count, 1
    jump ^loop

  block ^exit:
    %result = phi i64 [^entry: 0, ^loop: %count]
    return i64 %result
}

function @call_free_eh_load(%address : ptr, %choose : i64) -> i64 {
  block ^entry:
    eh_try ^handler
    eh_end
    %value = load i64 %address
    branch %choose, ^left, ^right

  block ^left:
    %left_value = binary add i64 %value, 1
    jump ^done

  block ^right:
    %right_value = binary sub i64 %value, 1
    jump ^done

  block ^done:
    %result = phi i64 [^left: %left_value, ^right: %right_value]
    return i64 %result

  block ^handler:
    return i64 99
}

function @call_crossing_eh_value(%address : ptr) -> i64 {
  block ^entry:
    eh_try ^handler
    %value = load i64 %address
    %ignored = call i64 @identity(7)
    eh_end
    jump ^done

  block ^done:
    %result = binary add i64 %value, 1
    return i64 %result

  block ^handler:
    return i64 99
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %head = addr @node1
    %walked = call i64 @walk_unavoidable(%head)
    %skipped = call i64 @walk_guarded(%head, 0)
    %number = addr @number
    %left = call i64 @call_free_eh_load(%number, 1)
    %right = call i64 @call_free_eh_load(%number, 0)
    %crossing = call i64 @call_crossing_eh_value(%number)
    %walk_bad = cmp ne i64 %walked, 2
    %skip_bad = cmp ne i64 %skipped, 0
    %left_bad = cmp ne i64 %left, 43
    %right_bad = cmp ne i64 %right, 41
    %crossing_bad = cmp ne i64 %crossing, 43
    %bad0 = binary or i64 %walk_bad, %skip_bad
    %bad1 = binary or i64 %left_bad, %right_bad
    %bad2 = binary or i64 %bad0, %bad1
    %bad = binary or i64 %bad2, %crossing_bad
    return i64 %bad
}
