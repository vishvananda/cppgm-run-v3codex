function @zero_index(%address : ptr) -> i64 {
  block ^entry:
    %same = index i64 %address, 0
    %value = load i64 %same
    return i64 %value
}

function @nonzero_index(%address : ptr) -> i64 {
  block ^entry:
    %next = index i64 %address, 1
    %value = load i64 %next
    return i64 %value
}
