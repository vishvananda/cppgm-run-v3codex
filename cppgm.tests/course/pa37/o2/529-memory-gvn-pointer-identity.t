function @same_pointer_phi(%address : ptr, %choose : i64) -> i64 {
  block ^entry:
    %first = load i64 %address
    branch %choose, ^left, ^right

  block ^left:
    jump ^join

  block ^right:
    jump ^join

  block ^join:
    %same = phi ptr [^left: %address, ^right: %address]
    %second = load i64 %same
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @different_pointer_phi(
    %left_address : ptr, %right_address : ptr, %choose : i64) -> i64 {
  block ^entry:
    %first = load i64 %left_address
    branch %choose, ^left, ^right

  block ^left:
    jump ^join

  block ^right:
    jump ^join

  block ^join:
    %selected = phi ptr [^left: %left_address, ^right: %right_address]
    %second = load i64 %selected
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @store_invalidates_pointer_phi(
    %address : ptr, %choose : i64) -> i64 {
  block ^entry:
    %first = load i64 %address
    branch %choose, ^write, ^keep

  block ^write:
    store i64 9, %address
    jump ^join

  block ^keep:
    jump ^join

  block ^join:
    %same = phi ptr [^write: %address, ^keep: %address]
    %second = load i64 %same
    %sum = binary add i64 %first, %second
    return i64 %sum
}
