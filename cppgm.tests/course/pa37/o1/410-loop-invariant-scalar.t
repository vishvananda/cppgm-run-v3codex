global @left : i64 = 7
global @right : i64 = 5
global @result : i64 = 0

function @main() -> i64 [role=entry] {
  block ^entry:
    %left = load i64 @left
    %right = load i64 @right
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit

  block ^body:
    %sum = binary add i64 %left, %right
    store i64 %sum, @result
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    %answer = load i64 @result
    return i64 %answer
}
