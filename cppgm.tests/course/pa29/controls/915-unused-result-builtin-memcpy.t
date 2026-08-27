global @source [storage=readonly, binding=internal] = {
  i8 11
  i8 22
  i8 33
  i8 44
  i8 55
}

global @destination [binding=internal] = {
  i8 0
  i8 0
  i8 0
  i8 0
  i8 99
}

global @copy_count : i64 [storage=readonly, binding=internal] = 4

function @memory_copy(
    %destination : ptr [alias=noalias],
    %source : ptr [alias=noalias],
    %count : i64) -> ptr
    [unwind=no, binding=strong,
     object=cppgm_builtin_memcpy] {
  block ^entry:
    jump ^test

  block ^test:
    %index = phi i64 [^entry: 0, ^body: %next_index]
    %done = cmp eq i64 %index, %count
    branch %done, ^return, ^body

  block ^body:
    %source_address = index i8 %source, %index
    %byte = load u8 %source_address
    %destination_address = index i8 %destination, %index
    store u8 %byte, %destination_address
    %next_index = binary add i64 %index, 1
    jump ^test

  block ^return:
    return ptr %destination
}

function @eligible_copy(%destination : ptr, %source : ptr,
                        %count : i64) -> void [unwind=no] {
  block ^entry:
    %ignored = call ptr @memory_copy(%destination, %source, %count)
    return void
}

function @used_result_copy(%destination : ptr, %source : ptr,
                           %count : i64) -> ptr [unwind=no] {
  block ^entry:
    %result = call ptr @memory_copy(%destination, %source, %count)
    return ptr %result
}

function @ordinary_copy(%destination : ptr, %source : ptr,
                        %count : i64) -> ptr [unwind=no] {
  block ^entry:
    return ptr %destination
}

function @ordinary_unused_copy(%destination : ptr, %source : ptr,
                               %count : i64) -> void [unwind=no] {
  block ^entry:
    %ignored = call ptr @ordinary_copy(%destination, %source, %count)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes,
                         unwind=no] {
  block ^entry:
    %destination = addr @destination
    %source = addr @source
    %count = load i64 @copy_count
    call void @eligible_copy(%destination, %source, %count)
    %first_address = index i8 %destination, 0
    %first = load u8 %first_address
    %first_bad = cmp ne u8 %first, 11
    %last_address = index i8 %destination, 3
    %last = load u8 %last_address
    %last_bad = cmp ne u8 %last, 44
    %sentinel_address = index i8 %destination, 4
    %sentinel = load u8 %sentinel_address
    %sentinel_bad = cmp ne u8 %sentinel, 99
    %copied_bad = binary or u8 %first_bad, %last_bad
    %bad = binary or u8 %copied_bad, %sentinel_bad
    %exit = convert zext i32 u8 %bad
    return i32 %exit
}
