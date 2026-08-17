global @single_result = {
  i64 17
}

global @pair_result = {
  i64 0
  i64 0
}

function @make_single() -> obj<8x8> {
  slot $value : obj<8x8>

  block ^entry:
    %address = addr $value
    store i64 23, %address
    return obj<8x8> $value
}

function @make_pair() -> obj<16x8> {
  slot $value : obj<16x8>

  block ^entry:
    %first_address = addr $value
    store i64 29, %first_address
    %second_address = index i8 %first_address, 8
    store i64 31, %second_address
    return obj<16x8> $value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %single = call obj<8x8> @make_single()
    %single_destination = addr @single_result
    copyobj 8x8 %single, %single_destination
    %pair = call obj<16x8> @make_pair()
    %pair_destination = addr @pair_result
    copyobj 16x8 %pair, %pair_destination
    %single_value = load i64 @single_result
    %first_value = load i64 @pair_result
    %pair_base = addr @pair_result
    %second_result_address = index i8 %pair_base, 8
    %second_value = load i64 %second_result_address
    %bad_single = cmp ne i64 %single_value, 23
    %bad_first = cmp ne i64 %first_value, 29
    %bad_second = cmp ne i64 %second_value, 31
    %bad_pair = binary or i64 %bad_first, %bad_second
    %bad = binary or i64 %bad_single, %bad_pair
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
