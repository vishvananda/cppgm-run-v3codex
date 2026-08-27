declare function @puts(%s : ptr) -> i32 [linkage=c, binding=strong]
declare global @shared_state : ptr [binding=weak]

function @helper() -> i64 [binding=internal] {
  block ^entry:
    return i64 11
}

function @main() -> i64 [role=entry, binding=strong] {
  block ^entry:
    %0 = call i64 @helper()
    return i64 %0
}
