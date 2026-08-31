function @read(%source : ptr) -> i64 {
  block ^entry:
    %value = load i64 %source
    return i64 %value
}

function @main() -> i64 [role=entry] {
  slot $source : i64

  block ^entry:
    %source = addr $source
    %value = call i64 @read(%source) [elision=copy]
    return i64 %value
}
