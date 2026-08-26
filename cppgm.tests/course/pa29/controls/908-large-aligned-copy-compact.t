function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<56x8>
  slot $destination : obj<56x8>

  block ^entry:
    %source = addr $source
    %source48 = index i8 [projection=field] %source, 48
    store i64 41, %source48
    copyobj 56x8 $source, $destination
    %destination = addr $destination
    %destination48 = index i8 [projection=field] %destination, 48
    %result = load i64 %destination48
    return i64 %result
}
