function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $byte : i8

  block ^entry:
    %base = addr $byte
    %unused = index i8 %base, 0
    return i32 0
}
