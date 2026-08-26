function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<48x8>
  slot $destination : obj<48x8>

  block ^entry:
    %source = addr $source
    store i64 1, %source
    %source8 = index i8 [projection=field] %source, 8
    store i64 2, %source8
    %source16 = index i8 [projection=field] %source, 16
    store i64 3, %source16
    %source24 = index i8 [projection=field] %source, 24
    store i64 4, %source24
    %source32 = index i8 [projection=field] %source, 32
    store i64 5, %source32
    %source40 = index i8 [projection=field] %source, 40
    store i64 37, %source40
    copyobj 48x8 $source, $destination
    %destination = addr $destination
    %destination40 = index i8 [projection=field] %destination, 40
    %result = load i64 %destination40
    return i64 %result
}
