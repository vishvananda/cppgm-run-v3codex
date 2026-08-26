function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<32x8>
  slot $destination : obj<32x8>

  block ^entry:
    %source = addr $source
    store i64 1, %source
    %source8 = index i8 [projection=field] %source, 8
    store i64 2, %source8
    %source16 = index i8 [projection=field] %source, 16
    store i64 3, %source16
    %source24 = index i8 [projection=field] %source, 24
    store i64 4, %source24
    copyobj 32x8 $source, $destination
    %destination = addr $destination
    %first = load i64 %destination
    %destination8 = index i8 [projection=field] %destination, 8
    %second = load i64 %destination8
    %destination16 = index i8 [projection=field] %destination, 16
    %third = load i64 %destination16
    %destination24 = index i8 [projection=field] %destination, 24
    %fourth = load i64 %destination24
    %sum0 = binary add i64 %first, %second
    %sum1 = binary add i64 %third, %fourth
    %sum = binary add i64 %sum0, %sum1
    return i64 %sum
}
