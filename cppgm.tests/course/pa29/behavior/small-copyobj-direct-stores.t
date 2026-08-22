global @src24 = {
  i64 11
  i64 22
  i64 33
}
global @dst24 = {
  zero 24
}
global @src7 = {
  i32 100
  i16 200
  i8 50
}
global @dst7 = {
  zero 8
}
global @src33 = {
  i64 1
  i64 2
  i64 3
  i64 4
  i8 5
}
global @dst33 = {
  zero 33
}

function @copy_all() -> void {
  block ^entry:
    %s24 = addr @src24
    %d24 = addr @dst24
    copyobj 24x8 %s24, %d24
    %s7 = addr @src7
    %d7 = addr @dst7
    copyobj 7x1 %s7, %d7
    %s33 = addr @src33
    %d33 = addr @dst33
    copyobj 33x1 %s33, %d33
    return void
}

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    call void @copy_all()
    %d24 = addr @dst24
    %a = load i64 %d24
    %p8 = index i8 [projection=field] %d24, 8
    %b = load i64 %p8
    %p16 = index i8 [projection=field] %d24, 16
    %c = load i64 %p16
    %s1 = binary add i64 %a, %b
    %s2 = binary add i64 %s1, %c
    %d7 = addr @dst7
    %w = load u32 %d7
    %pw4 = index i8 [projection=field] %d7, 4
    %h = load u16 %pw4
    %pw6 = index i8 [projection=field] %d7, 6
    %o = load u8 %pw6
    %w64 = convert zext i64 u32 %w
    %h64 = convert zext i64 u16 %h
    %o64 = convert zext i64 u8 %o
    %s3 = binary add i64 %s2, %w64
    %s4 = binary add i64 %s3, %h64
    %s5 = binary add i64 %s4, %o64
    %d33 = addr @dst33
    %e0 = load i64 %d33
    %q8 = index i8 [projection=field] %d33, 8
    %e1 = load i64 %q8
    %q16 = index i8 [projection=field] %d33, 16
    %e2 = load i64 %q16
    %q24 = index i8 [projection=field] %d33, 24
    %e3 = load i64 %q24
    %q32 = index i8 [projection=field] %d33, 32
    %e4 = load u8 %q32
    %e4w = convert zext i64 u8 %e4
    %s6 = binary add i64 %s5, %e0
    %s7v = binary add i64 %s6, %e1
    %s8 = binary add i64 %s7v, %e2
    %s9 = binary add i64 %s8, %e3
    %total = binary add i64 %s9, %e4w
    %wrong = cmp ne i64 %total, 431
    return i64 %wrong
}
