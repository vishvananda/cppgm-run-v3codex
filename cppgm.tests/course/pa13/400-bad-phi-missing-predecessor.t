function @main() -> i64 {
  block ^entry:
    %choose = const i64 1
    branch %choose, ^left, ^right

  block ^left:
    %a = const i64 1
    jump ^join

  block ^right:
    %b = const i64 2
    jump ^join

  block ^join:
    %result = phi i64 [^left: %a]
    return i64 %result
}
