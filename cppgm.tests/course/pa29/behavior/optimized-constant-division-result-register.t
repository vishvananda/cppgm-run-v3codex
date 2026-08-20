global @saved_power : i64 = 0
global @saved_magic : i64 = 0

function @divide_and_store(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %power = binary div i64 %value, 32
    store i64 %power, @saved_power
    %power_plus = binary add i64 %power, 1
    %magic = binary div i64 %value, 7
    store i64 %magic, @saved_magic
    %magic_plus = binary add i64 %magic, 1
    %power_saved = load i64 @saved_power
    %magic_saved = load i64 @saved_magic
    %saved = binary add i64 %power_saved, %magic_saved
    %adjusted = binary add i64 %power_plus, %magic_plus
    %result = binary add i64 %saved, %adjusted
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %positive = call i64 @divide_and_store(224)
    %bad_positive = cmp ne i64 %positive, 80
    %negative = call i64 @divide_and_store(-224)
    %bad_negative = cmp ne i64 %negative, -76
    %bad = binary or i64 %bad_positive, %bad_negative
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
