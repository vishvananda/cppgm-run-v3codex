global @cell = {
  i64 73
}

global @tag_source = {
  i8 1
}

function @observe(%base : ptr, %tag : obj<1x1>) -> i64 {
  block ^entry:
    %value = load i64 %base
    return i64 %value
}

function @forward(%base : ptr) -> i64 {
  slot $base_home : ptr
  slot $tag : obj<1x1>

  block ^entry:
    store ptr %base, $base_home
    %forwarded = load ptr $base_home
    %source = addr @tag_source
    %destination = addr $tag
    copyobj 1x1 %source, %destination
    %value = call i64 @observe(%forwarded, $tag)
    return i64 %value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @cell
    %value = call i64 @forward(%base)
    %wrong = cmp ne i64 %value, 73
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
