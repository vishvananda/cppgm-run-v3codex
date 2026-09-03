global @transfer : ptr

function @main() -> i64 [role=entry] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    call void @transfer(%destination, %source) [elision=copy] as (ptr, ptr) -> void
    return i64 0
}
