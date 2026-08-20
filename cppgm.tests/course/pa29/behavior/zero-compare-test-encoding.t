function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %v32 = copy u32 1
    %c32 = cmp ugt u32 %v32, 0
    branch %c32, ^check_i64, ^bad

  block ^check_i64:
    %v64 = copy i64 0
    %c64 = cmp eq i64 %v64, 0
    branch %c64, ^ok, ^bad

  block ^bad:
    return i32 1

  block ^ok:
    return i32 0
}
