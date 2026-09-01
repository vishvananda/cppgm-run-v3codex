function @target(%index_value : i64) -> i64 {
  block ^entry:
    return i64 %index_value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %function_pointer = addr @target
    %value = call i64 %function_pointer(0)
        as (%index_value : i64) -> i64 [query=stable_prefix]
    return i64 %value
}
