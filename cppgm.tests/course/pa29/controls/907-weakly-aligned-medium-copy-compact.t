function @main() -> i64 [role=entry, unwind=no] {
  slot $source : obj<48x1>
  slot $destination : obj<48x1>

  block ^entry:
    %source = addr $source
    %source40 = index i8 [projection=field] %source, 40
    store i64 39, %source40
    copyobj 48x1 $source, $destination
    %destination = addr $destination
    %destination40 = index i8 [projection=field] %destination, 40
    %result = load i64 %destination40
    return i64 %result
}
