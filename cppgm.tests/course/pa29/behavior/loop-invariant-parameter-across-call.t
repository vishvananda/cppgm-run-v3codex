global @state : i64 = 0
global @trash : i64 = 0

function @sink(%value : ptr) -> void {
  block ^entry:
    return void
}

function @write_and_clobber(%output : ptr, %value : i64, %other : ptr) -> void {
  block ^entry:
    store i64 %value, %output
    call void @sink(%other)
    return void
}

function @loop_store(%output : ptr, %other : ptr, %limit : i64) -> void {
  slot $iteration : i64

  block ^entry:
    store i64 0, $iteration
    jump ^condition

  block ^condition:
    %current = load i64 $iteration
    %continue = cmp ult i64 %current, %limit
    branch %continue, ^body, ^done

  block ^body:
    %forwarded = index i8 %output, 0
    call void @write_and_clobber(%forwarded, %current, %other)
    %next = binary add i64 %current, 1
    store i64 %next, $iteration
    jump ^condition

  block ^done:
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %state = addr @state
    %trash = addr @trash
    call void @loop_store(%state, %trash, 2)
    %state_value = load i64 %state
    %trash_value = load i64 %trash
    %wrong_state = cmp ne i64 %state_value, 1
    %wrong_trash = cmp ne i64 %trash_value, 0
    %wrong = binary or i64 %wrong_state, %wrong_trash
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
