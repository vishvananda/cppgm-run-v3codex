function @select_local_address(%take_left : i64) -> i32 {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    zeroinit 8x8 %address
    store i64 77, %address
    branch %take_left, ^left, ^right

  block ^left:
    %left_address = copy ptr %address
    jump ^join

  block ^right:
    %right_address = copy ptr %address
    jump ^join

  block ^join:
    %selected = phi ptr [^left: %left_address, ^right: %right_address]
    %loaded = load i64 %selected
    %result = convert trunc i32 i64 %loaded
    return i32 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %left = call i32 @select_local_address(1)
    %right = call i32 @select_local_address(0)
    %left_bad = cmp ne i32 %left, 77
    %right_bad = cmp ne i32 %right, 77
    %bad = binary or i64 %left_bad, %right_bad
    %exit = convert zext i32 i1 %bad
    return i32 %exit
}
