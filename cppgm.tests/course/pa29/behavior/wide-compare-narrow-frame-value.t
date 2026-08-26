function @check() -> i32 {
  slot $attempt : i64
  slot $byte : u8

  block ^entry:
    store i64 256, $attempt
    store u8 0, $byte
    %narrow = load u8 $byte
    jump ^test

  block ^test:
    %zero = cmp eq i64 %narrow, 0
    branch %zero, ^good, ^retry

  block ^retry:
    %attempt = load i64 $attempt
    %first = cmp eq i64 %attempt, 256
    branch %first, ^again, ^bad

  block ^again:
    store i64 257, $attempt
    jump ^test

  block ^good:
    return i32 0

  block ^bad:
    return i32 1
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %result = call i32 @check()
    return i32 %result
}
