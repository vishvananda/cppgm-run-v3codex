global @cppgm_priv_exc_top : ptr = 0
global @cppgm_priv_exc_value : ptr = 0

function @cppgm_priv_exc_unhandled() -> void {
  block ^entry:
    return void
}

function @clobber(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64) -> i64 {
  block ^entry:
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ef = binary add i64 %e, %f
    %abcd = binary add i64 %ab, %cd
    %result = binary add i64 %abcd, %ef
    return i64 %result
}

function @preserve(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64) -> i64 {
  slot $a : i64
  slot $b : i64
  slot $c : i64
  slot $d : i64
  slot $e : i64
  slot $f : i64

  block ^entry:
    store i64 %a, $a
    store i64 %b, $b
    store i64 %c, $c
    store i64 %d, $d
    store i64 %e, $e
    store i64 %f, $f
    %ignored = call i64 @clobber(101, 102, 103, 104, 105, 106)
    %aa = load i64 $a
    %bb = load i64 $b
    %cc = load i64 $c
    %dd = load i64 $d
    %ee = load i64 $e
    %ff = load i64 $f
    %ab = binary add i64 %aa, %bb
    %cd = binary add i64 %cc, %dd
    %ef = binary add i64 %ee, %ff
    %abcd = binary add i64 %ab, %cd
    %result = binary add i64 %abcd, %ef
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @preserve(1, 2, 3, 4, 5, 6)
    %wrong = cmp ne i64 %actual, 21
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
