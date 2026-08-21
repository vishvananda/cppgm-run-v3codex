global @g1 : i32 = 7
global @g2 : i64 = 2
global @g3 : i64 = 3
global @g4 : i64 = 4
global @g5 : i64 = 5

function @touch() -> void {
  block ^entry:
    return void
}

function @probe(%p1 : ptr, %p2 : ptr, %p3 : ptr,
                %p4 : ptr, %p5 : ptr) -> i64 {
  block ^entry:
    %atomic = atomic_load i32 %p1, 0
    call void @touch()
    %again32 = load i32 %p1
    %again = convert sext i64 i32 %again32
    %v2 = load i64 %p2
    %v3 = load i64 %p3
    %v4 = load i64 %p4
    %v5 = load i64 %p5
    %atomic64 = convert sext i64 i32 %atomic
    %s1 = binary add i64 %atomic64, %again
    %s2 = binary add i64 %s1, %v2
    %s3 = binary add i64 %s2, %v3
    %s4 = binary add i64 %s3, %v4
    %result = binary add i64 %s4, %v5
    return i64 %result
}

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %p1 = addr @g1
    %p2 = addr @g2
    %p3 = addr @g3
    %p4 = addr @g4
    %p5 = addr @g5
    %result = call i64 @probe(%p1, %p2, %p3, %p4, %p5)
    %wrong = cmp ne i64 %result, 28
    return i64 %wrong
}
