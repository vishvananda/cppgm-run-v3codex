global @take_outer : i64 = 1
global @take_path : i64 = 1
global @take_right : i64 = 0
global @repeat_inner : i64 = 0
global @result : i64 = 0

function @main() -> i64 {
  slot $value : i64

  block ^entry:
    jump ^outer_header

  block ^outer_header:
    %outer = load i64 @take_outer
    branch %outer, ^inner_entry, ^exit

  block ^inner_entry:
    jump ^inner_header

  block ^inner_header:
    %take = load i64 @take_path
    branch %take, ^choose, ^inner_merge

  block ^choose:
    %right = load i64 @take_right
    branch %right, ^right, ^left

  block ^left:
    store i64 7, $value
    jump ^selected

  block ^right:
    store i64 9, $value
    jump ^selected

  block ^selected:
    %picked = load i64 $value
    store i64 %picked, @result
    jump ^inner_merge

  block ^inner_merge:
    %again = load i64 @repeat_inner
    branch %again, ^inner_header, ^outer_latch

  block ^outer_latch:
    store i64 0, @take_outer
    jump ^outer_header

  block ^exit:
    %answer = load i64 @result
    return i64 %answer
}
