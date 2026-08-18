global @wide : i128 = 1267650600228229401496703205376

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %wide = load i128 @wide
    %wrong = cmp ne i128 %wide, 1267650600228229401496703205376
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
