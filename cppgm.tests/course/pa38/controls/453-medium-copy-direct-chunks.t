function @medium_copy() -> i64 [unwind=no] {
  slot $source : obj<61x1>
  slot $destination : obj<61x1>

  block ^entry:
    %source = addr $source
    %source_tail = index i8 [projection=field] %source, 53
    store volatile i64 39, %source_tail
    copyobj 61x1 $source, $destination
    %destination = addr $destination
    %destination_tail = index i8 [projection=field] %destination, 53
    %result = load volatile i64 %destination_tail
    return i64 %result
}

function @small_copy_control() -> i64 [unwind=no] {
  slot $source : obj<32x1>
  slot $destination : obj<32x1>

  block ^entry:
    %source = addr $source
    %source_tail = index i8 [projection=field] %source, 24
    store volatile i64 41, %source_tail
    copyobj 32x1 $source, $destination
    %destination = addr $destination
    %destination_tail = index i8 [projection=field] %destination, 24
    %result = load volatile i64 %destination_tail
    return i64 %result
}

function @large_copy_control() -> i64 [unwind=no] {
  slot $source : obj<65x1>
  slot $destination : obj<65x1>

  block ^entry:
    %source = addr $source
    %source_tail = index i8 [projection=field] %source, 57
    store volatile i64 43, %source_tail
    copyobj 65x1 $source, $destination
    %destination = addr $destination
    %destination_tail = index i8 [projection=field] %destination, 57
    %result = load volatile i64 %destination_tail
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %medium = call i64 @medium_copy()
    %small = call i64 @small_copy_control()
    %large = call i64 @large_copy_control()
    %bad_medium = cmp ne i64 %medium, 39
    %bad_small = cmp ne i64 %small, 41
    %bad_large = cmp ne i64 %large, 43
    %bad_pair = binary or i64 %bad_medium, %bad_small
    %bad = binary or i64 %bad_pair, %bad_large
    return i64 %bad
}
