function @copy_frame_object() -> i64 {
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

function @multiply_struct_strides(%value : i64) -> i64 {
  block ^entry:
    %times8 = binary mul i64 %value, 8
    %times24 = binary mul i64 %value, 24
    %times40 = binary mul i64 %value, 40
    %times72 = binary mul i64 %value, 72
    %fallback = binary mul i64 %value, 10
    %sum0 = binary add i64 %times8, %times24
    %sum1 = binary add i64 %times40, %times72
    %sum2 = binary add i64 %sum0, %sum1
    %sum = binary add i64 %sum2, %fallback
    return i64 %sum
}

function @multiply_wraps() -> i64 {
  block ^entry:
    %wrapped = binary mul i64 2305843009213693953, 8
    return i64 %wrapped
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %copied = call i64 @copy_frame_object()
    %strides = call i64 @multiply_struct_strides(-3)
    %wrapped = call i64 @multiply_wraps()
    %copy_bad = cmp ne i64 %copied, 66
    %strides_bad = cmp ne i64 %strides, -462
    %wrap_bad = cmp ne i64 %wrapped, 8
    %partial = binary or i64 %copy_bad, %strides_bad
    %bad = binary or i64 %partial, %wrap_bad
    return i64 %bad
}
