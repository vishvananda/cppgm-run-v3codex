global @one : i64 = 1
global @two : i64 = 2
global @three : i64 = 3
global @four : i64 = 4
global @five : i64 = 5
global @six : i64 = 6
global @seven : i64 = 7
global @observed : i64 = 0
global @cppgm_priv_exc_top : ptr = 0
global @cppgm_priv_exc_value : ptr = 0

function @cppgm_priv_exc_unhandled() -> void {
  block ^entry:
    return void
}

function @target(%a : ptr [pass=indirect_result], %b : ptr [pass=by_address], %c : ptr [pass=by_address], %d : ptr [pass=by_address], %e : ptr [pass=by_address], %f : ptr [pass=by_address], %g : ptr) -> void {
  block ^entry:
    %value = load i64 %f
    store i64 %value, @observed
    return void
}

function @consume(%value : ptr) -> void {
  block ^entry:
    return void
}

function @forward(%a : ptr [pass=indirect_result], %b : ptr [pass=by_address], %c : ptr [pass=by_address], %d : ptr [pass=by_address], %e : ptr [pass=by_address], %f : ptr [pass=by_address], %g : ptr) -> void {
  slot $temporary : i64

  block ^entry:
    eh_try ^unwind
    %temporary = addr $temporary
    call void @target(%temporary, %b, %c, %d, %e, %f, %g)
    eh_end
    jump ^done

  block ^unwind:
    resume

  block ^done:
    call void @consume(%a)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %a = addr @one
    %b = addr @two
    %c = addr @three
    %d = addr @four
    %e = addr @five
    %f = addr @six
    %g = addr @seven
    call void @forward(%a, %b, %c, %d, %e, %f, %g)
    %actual = load i64 @observed
    %wrong = cmp ne i64 %actual, 6
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
