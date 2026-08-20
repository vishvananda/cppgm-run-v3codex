global @saved : u8 = 0

function @byte_value() -> u8 {
  block ^entry:
    %value = const u8 173
    return u8 %value
}

function @store_result() -> i32 {
  block ^entry:
    %value = call u8 @byte_value()
    store u8 %value, @saved
    %loaded = load u8 @saved
    %wide = convert zext i32 u8 %loaded
    return i32 %wide
}

function @return_result() -> u8 {
  block ^entry:
    %value = call u8 @byte_value()
    return u8 %value
}

function @widen_result() -> i32 {
  block ^entry:
    %value = call u8 @byte_value()
    %wide = convert zext i32 u8 %value
    return i32 %wide
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %stored = call i32 @store_result()
    %returned = call u8 @return_result()
    %returned_wide = convert zext i32 u8 %returned
    %widened = call i32 @widen_result()
    %stored_bad = cmp ne i32 %stored, 173
    %returned_bad = cmp ne i32 %returned_wide, 173
    %widened_bad = cmp ne i32 %widened, 173
    %first_bad = binary or i64 %stored_bad, %returned_bad
    %bad = binary or i64 %first_bad, %widened_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
