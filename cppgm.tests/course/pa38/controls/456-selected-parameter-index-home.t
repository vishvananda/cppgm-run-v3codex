global @source [storage=readonly, binding=internal] = {
  i64 11
  i64 2
  i64 3
  i64 4
  i64 5
  i64 6
  i64 7
  i64 8
  i64 9
  i64 10
  i64 11
  i64 12
  i64 13
  i64 14
  i64 15
  i64 16
  i64 17
  i64 18
  i64 99
}

global @destination [binding=internal] = {
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
}

function @maybe_read(%address : ptr) -> i64 [no_inline=yes] {
  block ^entry:
    %value = load i64 %address
    %bad = cmp eq i64 %value, 0
    branch %bad, ^throw, ^return

  block ^throw:
    throw i64 1

  block ^return:
    return i64 %value
}

function @copy_then_index(%destination : ptr [alias=noalias],
                          %padding : ptr,
                          %source : ptr [alias=noalias]) -> i64 {
  block ^entry:
    eh_cleanup ^cleanup
    copyobj 144x8 %source, %destination
    %tail_address = index i8 [projection=field] %source, 144
    %result = call i64 @maybe_read(%tail_address)
    eh_end
    return i64 %result

  block ^cleanup:
    eh_end
    resume
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %destination = addr @destination
    %source = addr @source
    %result = call i64 @copy_then_index(
      %destination, %destination, %source)
    %first = load i64 %destination
    %tail_bad = cmp ne i64 %result, 99
    %copy_bad = cmp ne i64 %first, 11
    %bad = binary or i64 %tail_bad, %copy_bad
    return i64 %bad
}

