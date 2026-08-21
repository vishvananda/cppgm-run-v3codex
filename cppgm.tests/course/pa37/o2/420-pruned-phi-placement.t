global @take_work : i64 = 1
global @take_right : i64 = 0
global @result : i64 = 0

function @main() -> i64 {
  slot $value : i64

  block ^entry:
    %work = load i64 @take_work
    branch %work, ^choose, ^exit

  block ^choose:
    %right = load i64 @take_right
    branch %right, ^right, ^left

  block ^left:
    store i64 7, $value
    jump ^join

  block ^right:
    store i64 9, $value
    jump ^join

  block ^join:
    %selected = load i64 $value
    store i64 %selected, @result
    jump ^exit

  block ^exit:
    %answer = load i64 @result
    return i64 %answer
}
