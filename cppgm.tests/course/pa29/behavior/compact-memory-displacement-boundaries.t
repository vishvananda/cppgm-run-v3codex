global @bytes = {
  zero 512
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @bytes
    %center = index i8 %base, 256

    %negative_129 = index i8 %center, -129
    store i8 11, %negative_129
    %negative_128 = index i8 %center, -128
    store i8 22, %negative_128
    %negative_1 = index i8 %center, -1
    store i8 33, %negative_1
    %zero = index i8 %center, 0
    store i8 44, %zero
    %positive_1 = index i8 %center, 1
    store i8 55, %positive_1
    %positive_127 = index i8 %center, 127
    store i8 66, %positive_127
    %positive_128 = index i8 %center, 128
    store i8 77, %positive_128

    %value_negative_129 = load i8 %negative_129
    %bad_negative_129 = cmp ne i8 %value_negative_129, 11
    %value_negative_128 = load i8 %negative_128
    %bad_negative_128 = cmp ne i8 %value_negative_128, 22
    %bad_negative = binary or i64 %bad_negative_129, %bad_negative_128

    %value_negative_1 = load i8 %negative_1
    %bad_negative_1 = cmp ne i8 %value_negative_1, 33
    %value_zero = load i8 %zero
    %bad_zero = cmp ne i8 %value_zero, 44
    %bad_zero_edges = binary or i64 %bad_negative_1, %bad_zero

    %value_positive_1 = load i8 %positive_1
    %bad_positive_1 = cmp ne i8 %value_positive_1, 55
    %value_positive_127 = load i8 %positive_127
    %bad_positive_127 = cmp ne i8 %value_positive_127, 66
    %bad_positive_8 = binary or i64 %bad_positive_1, %bad_positive_127

    %value_positive_128 = load i8 %positive_128
    %bad_positive_128 = cmp ne i8 %value_positive_128, 77
    %bad_first = binary or i64 %bad_negative, %bad_zero_edges
    %bad_second = binary or i64 %bad_positive_8, %bad_positive_128
    %bad = binary or i64 %bad_first, %bad_second
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
