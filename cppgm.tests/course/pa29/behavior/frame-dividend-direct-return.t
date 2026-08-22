global @division_value : i64 = 160
global @guard_value : i64 = 7

function @may_unwind() -> void {
  block ^entry:
    return void
}

function @observe_values(%a : i64, %b : i64, %c : i64,
                         %d : i64, %e : i64) -> void [unwind=no] {
  block ^entry:
    return void
}

function @divide_frame_value(%a : i64, %b : i64, %c : i64,
                             %d : i64, %e : i64) -> i64 {
  block ^entry:
    %keep0 = binary add i64 %a, %b
    %keep1 = binary add i64 %b, %c
    %keep2 = binary add i64 %c, %d
    %keep3 = binary add i64 %d, %e
    %keep4 = binary add i64 %e, %a
    %partial = binary add i64 %keep0, %keep2
    %guard0 = binary add i64 %b, %d
    %guard1 = binary add i64 %a, %c
    %guard2 = binary add i64 %c, %e
    %guard3 = binary add i64 %a, %e
    %guard4 = binary add i64 %b, %e
    %guard5 = load i64 @guard_value
    %dividend = load i64 @division_value
    %guard6 = load i64 @guard_value
    eh_try ^cleanup
    call void @may_unwind()
    eh_end
    %consumed_guard0 = binary add i64 %guard0, 0
    %consumed_guard1 = binary add i64 %guard1, 0
    %consumed_guard2 = binary add i64 %guard2, 0
    %consumed_guard3 = binary add i64 %guard3, 0
    %consumed_guard4 = binary add i64 %guard4, 0
    %consumed_guard5 = binary add i64 %guard5, 0
    %consumed_guard6 = binary add i64 %guard6, 0
    %quotient = binary div i64 %dividend, 5
    return i64 %quotient

  block ^cleanup:
    resume

  block ^keep_live:
    call void @observe_values(%keep0, %keep1, %keep2, %keep3, %keep4)
    return i64 %partial
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %quotient = call i64 @divide_frame_value(10, 20, 30, 40, 50)
    %bad = cmp ne i64 %quotient, 32
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
