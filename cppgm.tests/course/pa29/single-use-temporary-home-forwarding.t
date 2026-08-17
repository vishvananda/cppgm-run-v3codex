global @single_observed : i64 = 0
global @shared_observed : i64 = 0

function @pressure(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %single = const i64 11
    store i64 %single, @single_observed
    %shared = const i64 13
    store i64 %shared, @shared_observed
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ea = binary add i64 %e, %a
    %bc = binary add i64 %b, %c
    %de = binary add i64 %d, %e
    %abcd = binary add i64 %ab, %cd
    %eabc = binary add i64 %ea, %bc
    %first_parameters = binary add i64 %abcd, %eabc
    %parameters = binary add i64 %first_parameters, %de
    %shared_second = binary add i64 %shared, 2
    %single_value = load i64 @single_observed
    %shared_value = load i64 @shared_observed
    %fixed = binary add i64 %first, %second
    %observed = binary add i64 %single_value, %shared_value
    %shared_all = binary add i64 %observed, %shared_second
    %subtotal = binary add i64 %fixed, %shared_all
    %all = binary add i64 %subtotal, %parameters
    return i64 %all
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %sum = call i64 @pressure(1, 2, 3, 4, 5)
    %bad = cmp ne i64 %sum, 369
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
