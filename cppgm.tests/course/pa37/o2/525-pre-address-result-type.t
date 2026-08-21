function @select_address(%choose : i64) -> i64 [no_inline=yes] {
  slot $value : obj<8x8>

  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    %left_address = addr $value
    store i64 11, %left_address
    jump ^join

  block ^right:
    %right_address = addr $value
    store i64 22, %right_address
    jump ^join

  block ^join:
    %joined_address = addr $value
    %result = load i64 %joined_address
    return i64 %result
}

function @select_partial_address(%choose : i64) -> i64 [no_inline=yes] {
  slot $value : obj<8x8>

  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    %left_address = addr $value
    store i64 33, %left_address
    jump ^join

  block ^right:
    zeroinit 8x8 $value
    jump ^join

  block ^join:
    %joined_address = addr $value
    %result = load i64 %joined_address
    return i64 %result
}
