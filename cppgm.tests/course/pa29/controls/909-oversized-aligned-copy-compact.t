function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<72x8>
  slot $destination : obj<72x8>

  block ^entry:
    %source = addr $source
    %source64 = index i8 [projection=field] %source, 64
    store i64 43, %source64
    copyobj 72x8 $source, $destination
    %destination = addr $destination
    %destination64 = index i8 [projection=field] %destination, 64
    %result = load i64 %destination64
    return i64 %result
}
