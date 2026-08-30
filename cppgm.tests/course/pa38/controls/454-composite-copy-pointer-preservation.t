global @source [storage=readonly, binding=internal] = {
  i64 11
  i64 22
  i64 33
  i64 44
}

global @destination [binding=internal] = {
  i64 0
  i64 0
  i64 0
  i64 0
}

global @frame_destination [binding=internal] = {
  i64 0
  i64 0
}

global @dynamic_destination [binding=internal] = {
  i64 0
}

global @used_destination [binding=internal] = {
  i64 0
}

function @memory_copy(
    %destination : ptr [alias=noalias],
    %source : ptr [alias=noalias],
    %count : i64) -> ptr
    [unwind=no, binding=strong, object=cppgm_builtin_memcpy] {
  block ^entry:
    jump ^test

  block ^test:
    %index = phi i64 [^entry: 0, ^body: %next]
    %done = cmp eq i64 %index, %count
    branch %done, ^return, ^body

  block ^body:
    %source_address = index i8 %source, %index
    %byte = load u8 %source_address
    %destination_address = index i8 %destination, %index
    store u8 %byte, %destination_address
    %next = binary add i64 %index, 1
    jump ^test

  block ^return:
    return ptr %destination
}

function @composite_move(%destination : ptr [alias=noalias],
                         %source : ptr [alias=noalias],
                         %count : i64) -> void [unwind=no] {
  block ^entry:
    copyobj 8x8 %source, %destination
    %source_middle = index i8 [projection=field] %source, 8
    %destination_middle = index i8 [projection=field] %destination, 8
    %ignored = call ptr @memory_copy(
      %destination_middle, %source_middle, %count)
    %source_tail = index i8 [projection=field] %source, 24
    %destination_tail = index i8 [projection=field] %destination, 24
    copyobj 8x8 %source_tail, %destination_tail
    return void
}

function @frame_composite_move(%destination : ptr [alias=noalias],
                               %source : ptr [alias=noalias],
                               %count : i64) -> i64 [unwind=no] {
  slot $local : obj<8x8>

  block ^entry:
    copyobj 8x8 %source, %destination
    %local = addr $local
    store i64 77, %local
    %ignored = call ptr @memory_copy(%destination, %local, %count)
    %source_tail = index i8 [projection=field] %source, 8
    %destination_tail = index i8 [projection=field] %destination, 8
    copyobj 8x8 %source_tail, %destination_tail
    %result = load i64 %destination
    return i64 %result
}

function @dynamic_only(%destination : ptr, %source : ptr,
                       %count : i64) -> void [unwind=no] {
  block ^entry:
    %ignored = call ptr @memory_copy(%destination, %source, %count)
    return void
}

function @used_result_guard(%destination : ptr, %source : ptr,
                            %count : i64) -> ptr [unwind=no] {
  block ^entry:
    copyobj 8x8 %source, %destination
    %result = call ptr @memory_copy(%destination, %source, %count)
    return ptr %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %source = addr @source
    %destination = addr @destination
    call void @composite_move(%destination, %source, 8)
    %first = load i64 %destination
    %destination_middle = index i8 %destination, 8
    %middle = load i64 %destination_middle
    %destination_tail = index i8 %destination, 24
    %tail = load i64 %destination_tail
    %bad_first = cmp ne i64 %first, 11
    %bad_middle = cmp ne i64 %middle, 22
    %bad_tail = cmp ne i64 %tail, 44

    %frame_destination = addr @frame_destination
    %frame_result = call i64 @frame_composite_move(
      %frame_destination, %source, 8)
    %frame_bad = cmp ne i64 %frame_result, 77

    %dynamic_destination = addr @dynamic_destination
    %source_third = index i8 %source, 16
    call void @dynamic_only(%dynamic_destination, %source_third, 8)
    %dynamic_value = load i64 %dynamic_destination
    %dynamic_bad = cmp ne i64 %dynamic_value, 33

    %used_destination = addr @used_destination
    %used_result = call ptr @used_result_guard(
      %used_destination, %source, 8)
    %used_pointer_bad = cmp ne ptr %used_result, %used_destination
    %used_value = load i64 %used_destination
    %used_value_bad = cmp ne i64 %used_value, 11
    %used_bad = binary or i64 %used_pointer_bad, %used_value_bad

    %bad_pair = binary or i64 %bad_first, %bad_middle
    %bad_three = binary or i64 %bad_pair, %bad_tail
    %bad_four = binary or i64 %bad_three, %frame_bad
    %bad_five = binary or i64 %bad_four, %dynamic_bad
    %bad = binary or i64 %bad_five, %used_bad
    return i64 %bad
}
