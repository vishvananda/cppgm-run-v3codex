global @left [binding=internal] = { i64 11 i64 13 i64 17 i64 19 }
global @right [binding=internal] = { i64 23 i64 29 i64 31 i64 37 }
global @wide_left [binding=internal] = {
  i64 41 i64 43 i64 47 i64 53 i64 59
  i64 61 i64 67 i64 71 i64 73 i64 79
}
global @wide_right [binding=internal] = {
  i64 83 i64 89 i64 97 i64 101 i64 103
  i64 107 i64 109 i64 113 i64 127 i64 131
}
global @observable : i64 [binding=internal] = 0

function @aggregate_swap(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<32x8>
  block ^entry:
    %stage_address = addr $stage
    copyobj 32x8 %first, %stage_address
    copyobj 32x8 %second, %first
    copyobj 32x8 %stage_address, %second
    return void
}

function @fieldwise_swap(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<80x8>
  slot $discarded_value : obj<80x8>
  block ^entry:
    %stage_address = addr $stage
    copyobj 16x8 %first, %stage_address
    %first16 = index i8 [projection=field] %first, 16
    %stage16 = index i8 [projection=field] %stage_address, 16
    %value16 = load i64 %first16
    store i64 %value16, %stage16
    %first24 = index i8 [projection=field] %first, 24
    %stage24 = index i8 [projection=field] %stage_address, 24
    %value24 = load i64 %first24
    store i64 %value24, %stage24
    %first32 = index i8 [projection=field] %first, 32
    %stage32 = index i8 [projection=field] %stage_address, 32
    %value32 = load i64 %first32
    store i64 %value32, %stage32
    %first40 = index i8 [projection=field] %first, 40
    %stage40 = index i8 [projection=field] %stage_address, 40
    %value40 = load i64 %first40
    store i64 %value40, %stage40
    %first48 = index i8 [projection=field] %first, 48
    %stage48 = index i8 [projection=field] %stage_address, 48
    %value48 = load i64 %first48
    store i64 %value48, %stage48
    %first56 = index i8 [projection=field] %first, 56
    %stage56 = index i8 [projection=field] %stage_address, 56
    %value56 = load i64 %first56
    store i64 %value56, %stage56
    %first64 = index i8 [projection=field] %first, 64
    %stage64 = index i8 [projection=field] %stage_address, 64
    %value64 = load i64 %first64
    store i64 %value64, %stage64
    %first72 = index i8 [projection=field] %first, 72
    %stage72 = index i8 [projection=field] %stage_address, 72
    %value72 = load i64 %first72
    store i64 %value72, %stage72
    %discarded = addr $discarded_value
    zeroinit 80x8 %discarded
    copyobj 80x8 %discarded, %first
    copyobj 80x8 %second, %first
    copyobj 80x8 %stage_address, %second
    return void
}

function @retain_incomplete_stage(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<32x8>
  block ^entry:
    %stage_address = addr $stage
    zeroinit 32x8 %stage_address
    copyobj 16x8 %first, %stage_address
    copyobj 32x8 %second, %first
    copyobj 32x8 %stage_address, %second
    return void
}

function @retain_volatile_capture(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<16x8>
  block ^entry:
    %stage_address = addr $stage
    copyobj 8x8 %first, %stage_address
    %first_tail = index i8 [projection=field] %first, 8
    %stage_tail = index i8 [projection=field] %stage_address, 8
    %tail = load volatile i64 %first_tail
    store i64 %tail, %stage_tail
    copyobj 16x8 %second, %first
    copyobj 16x8 %stage_address, %second
    return void
}

function @observe(%address : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    %value = load volatile i64 %address
    store volatile i64 %value, @observable
    return void
}

function @retain_escaped_stage(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<32x8>
  block ^entry:
    %stage_address = addr $stage
    copyobj 32x8 %first, %stage_address
    call void @observe(%stage_address)
    copyobj 32x8 %second, %first
    copyobj 32x8 %stage_address, %second
    return void
}

function @retain_nonterminal_swap(%first : ptr, %second : ptr) -> void
    [no_inline=yes, unwind=no] {
  slot $stage : obj<32x8>
  block ^entry:
    %stage_address = addr $stage
    copyobj 32x8 %first, %stage_address
    copyobj 32x8 %second, %first
    copyobj 32x8 %stage_address, %second
    store volatile i64 1, @observable
    return void
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    %left_address = addr @left
    %right_address = addr @right
    call void @aggregate_swap(%left_address, %right_address)
    call void @aggregate_swap(%left_address, %left_address)
    %left_first = load i64 %left_address
    %left_last_address = index i8 [projection=field] %left_address, 24
    %left_last = load i64 %left_last_address
    %right_first = load i64 %right_address
    %right_last_address = index i8 [projection=field] %right_address, 24
    %right_last = load i64 %right_last_address
    %left_first_ok = cmp eq i64 %left_first, 23
    %left_last_ok = cmp eq i64 %left_last, 37
    %right_first_ok = cmp eq i64 %right_first, 11
    %right_last_ok = cmp eq i64 %right_last, 19
    %short_left_ok = binary and i64 %left_first_ok, %left_last_ok
    %short_right_ok = binary and i64 %right_first_ok, %right_last_ok
    %short_ok = binary and i64 %short_left_ok, %short_right_ok
    branch %short_ok, ^wide, ^fail
  block ^wide:
    %wide_left_address = addr @wide_left
    %wide_right_address = addr @wide_right
    call void @fieldwise_swap(%wide_left_address, %wide_right_address)
    %wide_left_first = load i64 %wide_left_address
    %wide_left_last_address = index i8 [projection=field] %wide_left_address, 72
    %wide_left_last = load i64 %wide_left_last_address
    %wide_right_first = load i64 %wide_right_address
    %wide_right_last_address = index i8 [projection=field] %wide_right_address, 72
    %wide_right_last = load i64 %wide_right_last_address
    %wide_left_first_ok = cmp eq i64 %wide_left_first, 83
    %wide_left_last_ok = cmp eq i64 %wide_left_last, 131
    %wide_right_first_ok = cmp eq i64 %wide_right_first, 41
    %wide_right_last_ok = cmp eq i64 %wide_right_last, 79
    %wide_left_ok = binary and i64 %wide_left_first_ok, %wide_left_last_ok
    %wide_right_ok = binary and i64 %wide_right_first_ok, %wide_right_last_ok
    %wide_ok = binary and i64 %wide_left_ok, %wide_right_ok
    branch %wide_ok, ^pass, ^fail
  block ^pass:
    return i32 0
  block ^fail:
    return i32 1
}
