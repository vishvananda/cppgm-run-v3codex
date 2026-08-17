global @value = {
  i64 47
}

function @read(%address : ptr) -> i64 {
  block ^entry:
    %value = load i64 %address
    return i64 %value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %direct = addr @value
    %first = call i64 @read(%direct)
    %shared = addr @value
    %second = call i64 @read(%shared)
    %third = call i64 @read(%shared)
    %partial = binary add i64 %first, %second
    %total = binary add i64 %partial, %third
    return i64 %total
}
