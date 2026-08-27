global @expected_receiver : ptr = 0
global @expected_syntax : ptr = 0
global @expected_scope : ptr = 0
global @expected_specifiers : ptr = 0

function @check_forwarded(%ret : ptr [pass=indirect_result], %receiver : ptr, %syntax : ptr [pass=by_address], %scope : ptr, %specifiers : ptr) -> void {
  block ^entry:
    %want_receiver = load ptr @expected_receiver
    %want_syntax = load ptr @expected_syntax
    %want_scope = load ptr @expected_scope
    %want_specifiers = load ptr @expected_specifiers
    %receiver_ok = cmp eq ptr %receiver, %want_receiver
    %syntax_ok = cmp eq ptr %syntax, %want_syntax
    %scope_ok = cmp eq ptr %scope, %want_scope
    %specifiers_ok = cmp eq ptr %specifiers, %want_specifiers
    %first = binary and i64 %receiver_ok, %syntax_ok
    %second = binary and i64 %scope_ok, %specifiers_ok
    %all = binary and i64 %first, %second
    %failed = cmp eq i64 %all, 0
    %exit = convert trunc i32 i64 %failed
    store i32 %exit, %ret
    return void
}

function @forward_after_compare(%ret : ptr [pass=indirect_result], %receiver : ptr, %syntax : ptr [pass=by_address], %scope : ptr, %specifiers : ptr, %defer : u8) -> void {
  slot $selected : i64

  block ^entry:
    %member = index i8 [projection=field] %receiver, 136
    %prior = load u8 %member
    branch %prior, ^short, ^rhs

  block ^rhs:
    %selected = cmp ne i64 %defer, 0
    store i64 %selected, $selected
    jump ^merge

  block ^short:
    store i64 1, $selected
    jump ^merge

  block ^merge:
    %value = load i64 $selected
    store u8 %value, %member
    call void @check_forwarded(%ret, %receiver, %syntax, %scope, %specifiers)
    store u8 %prior, %member
    return void
}

function @main() -> i32 [role=entry] {
  slot $result : obj<4x4>
  slot $object : obj<144x8>
  slot $syntax : obj<8x8>
  slot $scope : obj<8x8>
  slot $specifiers : obj<8x8>

  block ^entry:
    %result = addr $result
    %object = addr $object
    %syntax = addr $syntax
    %scope = addr $scope
    %specifiers = addr $specifiers
    %member = index i8 [projection=field] %object, 136
    store u8 0, %member
    store ptr %object, @expected_receiver
    store ptr %syntax, @expected_syntax
    store ptr %scope, @expected_scope
    store ptr %specifiers, @expected_specifiers
    call void @forward_after_compare(%result, %object, %syntax, %scope, %specifiers, 1)
    %exit = load i32 %result
    return i32 %exit
}
