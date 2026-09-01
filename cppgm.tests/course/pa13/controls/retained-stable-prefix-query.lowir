global @values [binding=internal] = {
  i64 5
  i64 7
  i64 11
}

function @prefix_query(%state : ptr, %index_value : i64) -> i64
    [binding=internal, query=stable_prefix] {
  block ^entry:
    %offset = binary mul i64 %index_value, 8
    %address = index i8 %state, %offset
    %value = load i64 %address
    return i64 %value
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %state = addr @values
    %first = call i64 @prefix_query(%state, 0)
    %higher = call i64 @prefix_query(%state, 2)
    %again = call i64 @prefix_query(%state, 0)
    %sum0 = binary add i64 %first, %higher
    %sum1 = binary add i64 %sum0, %again
    %bad = cmp ne i64 %sum1, 21
    return i64 %bad
}
