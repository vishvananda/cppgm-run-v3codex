function @raise() -> i64 [binding=weak] {
  block ^entry:
    throw i64 7
}

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %result = call i64 @raise()
    return i64 %result
}
