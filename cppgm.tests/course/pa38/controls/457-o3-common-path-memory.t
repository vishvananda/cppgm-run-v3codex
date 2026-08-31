global @copy_storage [binding=internal] = {
  i64 11
  i64 13
  i64 0
  i64 0
}
global @overlap_storage [binding=internal] = {
  i64 23
  i64 29
  i64 0
}
global @different_source [binding=internal] = {
  i64 31
  i64 37
}
global @different_destination [binding=internal] = {
  i64 0
  i64 0
}
global @volatile_storage [binding=internal] = {
  i64 41
  i64 43
  i64 0
  i64 0
}

global @guard_value : i64 [binding=internal] = 17
global @guard_pointer : ptr [binding=internal] = addr @guard_value
global @null_guard_pointer : ptr [binding=internal] = 0

function @copy_adjacent_fields(%object : ptr) -> void [unwind=no] {
  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load i64 %source_zero !dbg(common_path.cpp, 3, 5)
    %destination_zero = index i8 [projection=field] %object, 16
    store i64 %first, %destination_zero !dbg(common_path.cpp, 4, 5)
    %source_one = index i8 [projection=field] %object, 8
    %second = load i64 %source_one !dbg(common_path.cpp, 5, 5)
    %destination_one = index i8 [projection=field] %object, 24
    store i64 %second, %destination_one !dbg(common_path.cpp, 6, 5)
    return void
}

function @copy_through_private_stages(%object : ptr) -> void [unwind=no] {
  slot $first_stage : i64
  slot $second_stage : i64

  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load i64 %source_zero !dbg(common_path.cpp, 11, 5)
    store i64 %first, $first_stage !dbg(common_path.cpp, 12, 5)
    %first_staged = load i64 $first_stage !dbg(common_path.cpp, 13, 5)
    %destination_zero = index i8 [projection=field] %object, 16
    store i64 %first_staged, %destination_zero !dbg(common_path.cpp, 14, 5)
    %source_one = index i8 [projection=field] %object, 8
    %second = load i64 %source_one !dbg(common_path.cpp, 15, 5)
    store i64 %second, $second_stage !dbg(common_path.cpp, 16, 5)
    %second_staged = load i64 $second_stage !dbg(common_path.cpp, 17, 5)
    %destination_one = index i8 [projection=field] %object, 24
    store i64 %second_staged, %destination_one !dbg(common_path.cpp, 18, 5)
    return void
}

function @copy_overlap_guard(%object : ptr) -> void [unwind=no] {
  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load i64 %source_zero
    %destination_zero = index i8 [projection=field] %object, 8
    store i64 %first, %destination_zero
    %source_one = index i8 [projection=field] %object, 8
    %second = load i64 %source_one
    %destination_one = index i8 [projection=field] %object, 16
    store i64 %second, %destination_one
    return void
}

function @copy_different_bases(%source : ptr,
                               %destination : ptr) -> void [unwind=no] {
  block ^entry:
    %first = load i64 %source
    store i64 %first, %destination
    %source_one = index i8 [projection=field] %source, 8
    %second = load i64 %source_one
    %destination_one = index i8 [projection=field] %destination, 8
    store i64 %second, %destination_one
    return void
}

function @copy_volatile_guard(%object : ptr) -> void [unwind=no] {
  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load volatile i64 %source_zero
    %destination_zero = index i8 [projection=field] %object, 16
    store volatile i64 %first, %destination_zero
    %source_one = index i8 [projection=field] %object, 8
    %second = load volatile i64 %source_one
    %destination_one = index i8 [projection=field] %object, 24
    store volatile i64 %second, %destination_one
    return void
}

function @copy_reused_scalar_guard(%object : ptr) -> i64 [unwind=no] {
  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load i64 %source_zero
    %destination_zero = index i8 [projection=field] %object, 16
    store i64 %first, %destination_zero
    %source_one = index i8 [projection=field] %object, 8
    %second = load i64 %source_one
    %destination_one = index i8 [projection=field] %object, 24
    store i64 %second, %destination_one
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @copy_shared_stage_guard(%object : ptr) -> i64 [unwind=no] {
  slot $first_stage : i64
  slot $second_stage : i64

  block ^entry:
    %source_zero = index i8 [projection=field] %object, 0
    %first = load i64 %source_zero
    store i64 %first, $first_stage
    %first_staged = load i64 $first_stage
    %destination_zero = index i8 [projection=field] %object, 16
    store i64 %first_staged, %destination_zero
    %source_one = index i8 [projection=field] %object, 8
    %second = load i64 %source_one
    store i64 %second, $second_stage
    %second_staged = load i64 $second_stage
    %destination_one = index i8 [projection=field] %object, 24
    store i64 %second_staged, %destination_one
    %observed = load volatile i64 $first_stage
    return i64 %observed
}

function @guarded_private_stage(%pointer_slot : ptr,
                                %enabled : i64) -> i64 [unwind=no] {
  slot $stage : ptr

  block ^entry:
    %pointer = load ptr %pointer_slot !dbg(common_path.cpp, 52, 5)
    store ptr %pointer, $stage !dbg(common_path.cpp, 53, 5)
    %tested = load ptr $stage !dbg(common_path.cpp, 54, 5)
    branch %tested, ^check_enabled, ^bypass !dbg(common_path.cpp, 55, 5)

  block ^check_enabled:
    %disabled = cmp eq i64 %enabled, 0
    branch %disabled, ^bypass, ^consume

  block ^consume:
    %consumed = load ptr $stage !dbg(common_path.cpp, 61, 5)
    %value = load i64 %consumed
    return i64 %value

  block ^bypass:
    return i64 0
}

function @guarded_volatile_stage(%pointer_slot : ptr) -> i64 [unwind=no] {
  slot $stage : ptr

  block ^entry:
    %pointer = load ptr %pointer_slot
    store volatile ptr %pointer, $stage
    %tested = load volatile ptr $stage
    branch %tested, ^consume, ^bypass

  block ^consume:
    %consumed = load volatile ptr $stage
    %value = load i64 %consumed
    return i64 %value

  block ^bypass:
    return i64 0
}

function @observe_stage(%address : ptr) -> ptr [unwind=no] {
  block ^entry:
    %value = load ptr %address
    return ptr %value
}

function @guarded_escaping_stage(%pointer_slot : ptr) -> i64 [unwind=no] {
  slot $stage : ptr

  block ^entry:
    %stage_address = addr $stage
    %pointer = load ptr %pointer_slot
    store ptr %pointer, $stage
    %tested = load ptr $stage
    branch %tested, ^consume, ^bypass

  block ^consume:
    %consumed = call ptr @observe_stage(%stage_address)
    %value = load i64 %consumed
    return i64 %value

  block ^bypass:
    return i64 0
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %copy_storage = addr @copy_storage
    call void @copy_adjacent_fields(%copy_storage)
    call void @copy_through_private_stages(%copy_storage)
    %copy_first_address = index i8 [projection=field] %copy_storage, 16
    %copy_first = load i64 %copy_first_address
    %copy_second_address = index i8 [projection=field] %copy_storage, 24
    %copy_second = load i64 %copy_second_address
    %guard_pointer = addr @guard_pointer
    %guard = call i64 @guarded_private_stage(%guard_pointer, 1)
    %null_guard_pointer = addr @null_guard_pointer
    %null_guard = call i64 @guarded_private_stage(%null_guard_pointer, 1)
    %volatile_guard = call i64 @guarded_volatile_stage(%guard_pointer)
    %escaping_guard = call i64 @guarded_escaping_stage(%guard_pointer)
    %overlap_storage = addr @overlap_storage
    call void @copy_overlap_guard(%overlap_storage)
    %overlap_result_address = index i8 [projection=field] %overlap_storage, 16
    %overlap_result = load i64 %overlap_result_address
    %different_source = addr @different_source
    %different_destination = addr @different_destination
    call void @copy_different_bases(%different_source, %different_destination)
    %different_result_address = index i8 [projection=field] %different_destination, 8
    %different_result = load i64 %different_result_address
    %volatile_storage = addr @volatile_storage
    call void @copy_volatile_guard(%volatile_storage)
    %volatile_result_address = index i8 [projection=field] %volatile_storage, 24
    %volatile_result = load i64 %volatile_result_address
    %reused_scalar = call i64 @copy_reused_scalar_guard(%copy_storage)
    %shared_stage = call i64 @copy_shared_stage_guard(%copy_storage)
    %bad_first = cmp ne i64 %copy_first, 11
    %bad_second = cmp ne i64 %copy_second, 13
    %bad_guard = cmp ne i64 %guard, 17
    %bad_null_guard = cmp ne i64 %null_guard, 0
    %bad_volatile_guard = cmp ne i64 %volatile_guard, 17
    %bad_escaping_guard = cmp ne i64 %escaping_guard, 17
    %bad_overlap = cmp ne i64 %overlap_result, 23
    %bad_different = cmp ne i64 %different_result, 37
    %bad_volatile_copy = cmp ne i64 %volatile_result, 43
    %bad_reused_scalar = cmp ne i64 %reused_scalar, 24
    %bad_shared_stage = cmp ne i64 %shared_stage, 11
    %bad_copy = binary or i64 %bad_first, %bad_second
    %bad_guard_pair = binary or i64 %bad_guard, %bad_null_guard
    %bad_guard_controls = binary or i64 %bad_volatile_guard, %bad_escaping_guard
    %bad_copy_controls_one = binary or i64 %bad_overlap, %bad_different
    %bad_copy_controls_two = binary or i64 %bad_volatile_copy, %bad_reused_scalar
    %bad_copy_controls_three = binary or i64 %bad_shared_stage, %bad_copy_controls_two
    %bad_controls_one = binary or i64 %bad_guard_pair, %bad_guard_controls
    %bad_controls_two = binary or i64 %bad_copy_controls_one, %bad_copy_controls_three
    %bad_controls = binary or i64 %bad_controls_one, %bad_controls_two
    %bad = binary or i64 %bad_copy, %bad_controls
    return i64 %bad
}
