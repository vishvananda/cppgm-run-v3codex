function @choose(%condition : i64) -> i64 {
  block ^entry:
    branch %condition, ^left, ^right

  block ^left:
    return i64 11

  block ^right:
    return i64 22
}

function @caller(%use_call : i64, %condition : i64) -> i64 [binding=strong] {
  block ^entry:
    branch %use_call, ^with_call, ^other

  block ^with_call:
    %chosen = call i64 @choose(%condition)
    jump ^join

  block ^other:
    jump ^join

  block ^join:
    %result = phi i64 [^with_call: %chosen, ^other: 33]
    return i64 %result
}
