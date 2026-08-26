function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<24x8>
  slot $destination : obj<24x8>

  block ^entry:
    %source = addr $source
    store i64 11, %source
    %source8 = index i8 [projection=field] %source, 8
    store i64 22, %source8
    %source16 = index i8 [projection=field] %source, 16
    store i64 33, %source16
    copyobj 24x8 $source, $destination
    %destination = addr $destination
    %first = load i64 %destination
    %destination8 = index i8 [projection=field] %destination, 8
    %second = load i64 %destination8
    %destination16 = index i8 [projection=field] %destination, 16
    %third = load i64 %destination16
    %partial = binary add i64 %first, %second
    %sum = binary add i64 %partial, %third
    return i64 %sum
}
