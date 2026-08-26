function @shared_loop(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^body: %next]
    %more = cmp ult i64 %index, %limit
    branch %more, ^body, ^done

  block ^body:
    %next = binary add i64 %index, 1
    jump ^loop

  block ^done:
    return i64 %index
}

function @shared_a(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @shared_loop(%limit)
    return i64 %result
}

function @shared_b(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @shared_loop(%limit)
    return i64 %result
}

function @single_loop(%limit : i64) -> i64 [binding=internal] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^body: %next]
    %more = cmp ult i64 %index, %limit
    branch %more, ^body, ^done

  block ^body:
    %next = binary add i64 %index, 1
    jump ^loop

  block ^done:
    return i64 %index
}

function @single_caller(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @single_loop(%limit)
    return i64 %result
}

function @hinted_loop(%limit : i64) -> i64
    [binding=strong, inline_hint=yes] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^body: %next]
    %more = cmp ult i64 %index, %limit
    branch %more, ^body, ^done

  block ^body:
    %next = binary add i64 %index, 1
    jump ^loop

  block ^done:
    return i64 %index
}

function @hinted_a(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @hinted_loop(%limit)
    return i64 %result
}

function @hinted_b(%limit : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @hinted_loop(%limit)
    return i64 %result
}
