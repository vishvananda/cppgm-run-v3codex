global @short_word [storage=readonly, binding=internal] = {
  i8 104
  i8 101
  i8 108
  i8 108
  i8 111
  i8 0
}

global @long_word [storage=readonly, binding=internal] = {
  i8 97
  i8 98
  i8 99
  i8 100
  i8 101
  i8 102
  i8 103
  i8 104
  i8 105
  i8 106
  i8 107
  i8 108
  i8 109
  i8 110
  i8 111
  i8 112
  i8 113
  i8 114
  i8 115
  i8 116
  i8 0
}

function @strlen(%data : ptr) -> i64 [effects=readonly, unwind=no, binding=strong, object=cppgm_builtin_strlen] {
  block ^entry:
    jump ^test

  block ^test:
    %length = phi i64 [^entry: 0, ^next: %next_length]
    %cursor = index i8 %data, %length
    %byte = load u8 %cursor
    %done = cmp eq u8 %byte, 0
    branch %done, ^return, ^next

  block ^next:
    %next_length = binary add i64 %length, 1
    jump ^test

  block ^return:
    return i64 %length
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %short_data = addr @short_word
    %short_length = call i64 @strlen(%short_data)
    %long_data = addr @long_word
    %long_length = call i64 @strlen(%long_data)
    %total = binary add i64 %short_length, %long_length
    %exit = convert trunc i32 i64 %total
    return i32 %exit
}
