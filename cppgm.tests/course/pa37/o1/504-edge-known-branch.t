function @fold_false_edge(%condition : i64) -> i64 {
  block ^entry:
    branch %condition, ^body, ^exit

  block ^body:
    jump ^entry

  block ^exit:
    branch %condition, ^impossible, ^selected

  block ^impossible:
    jump ^merge

  block ^selected:
    jump ^merge

  block ^merge:
    %result = phi i64 [^impossible: 99, ^selected: 7]
    return i64 %result
}

function @retain_removed_phi_edge(%condition : i64) -> i64 {
  block ^entry:
    branch %condition, ^phi_target, ^known_false

  block ^known_false:
    branch %condition, ^phi_target, ^selected

  block ^phi_target:
    %value = phi i64 [^entry: 11, ^known_false: 12]
    return i64 %value

  block ^selected:
    return i64 3
}
