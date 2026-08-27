declare function @pure() -> i64 [effects=readnone, unwind=no]
declare function @sink() -> i64 [unwind=no]

function @main(%fn : ptr) -> i64 {
  block ^entry:
    %0 = call i64 @pure()
    %1 = call i64 %fn() as () -> i64 [effects=readnone, unwind=no]
    %2 = call i64 @sink()
    return i64 0
}
