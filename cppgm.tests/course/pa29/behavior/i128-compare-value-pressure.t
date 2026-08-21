global @wide : i128 = 1
global @g1 : i64 = 1
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
    %wide = load i128 @wide
    %positive = cmp gt i128 %wide, 0
    call void @touch()
    %v1 = load i64 %p1
    %v2 = load i64 %p2
    %v3 = load i64 %p3
    %v4 = load i64 %p4
    %v5 = load i64 %p5
    %s1 = binary add i64 %v1, %v2
    %s2 = binary add i64 %s1, %v3
    %s3 = binary add i64 %s2, %v4
    %s4 = binary add i64 %s3, %v5
    %result = binary add i64 %s4, %positive
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
    %wrong = cmp ne i64 %result, 16
    return i64 %wrong
}
