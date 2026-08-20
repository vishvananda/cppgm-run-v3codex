global @result = {
  i64 0
  i64 0
}

function @make_pair() -> obj<16x8> {
  slot $value : obj<16x8>

  block ^entry:
    %address = addr $value
    store i64 17, %address
    %second = index i8 %address, 8
    store i64 23, %second
    return obj<16x8> $value
}

function @forward_pair() -> obj<16x8> {
  slot $result : obj<16x8>

  block ^entry:
    %destination = addr $result
    %value = call obj<16x8> @make_pair()
    copyobj 16x8 %value, %destination
    return obj<16x8> $result
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %value = call obj<16x8> @forward_pair()
    %destination = addr @result
    copyobj 16x8 %value, %destination
    %first = load i64 @result
    %base = addr @result
    %second_address = index i8 %base, 8
    %second = load i64 %second_address
    %first_bad = cmp ne i64 %first, 17
    %second_bad = cmp ne i64 %second, 23
    %bad = binary or i64 %first_bad, %second_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
