function @main(%x : i64) -> i64 {
  block ^entry:
    %result = call i64 @wrapper(%x)
    return i64 %result
}

function @wrapper(%x : i64) -> i64 [unwind=no] {
  block ^entry:
    eh_try ^dispatch
    %result = call i64 @leaf(%x)
    eh_end
    return i64 %result

  block ^dispatch:
    resume
}

function @leaf(%x : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %x
}
