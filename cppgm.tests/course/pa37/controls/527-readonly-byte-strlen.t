global @readonly_word [storage=readonly, binding=internal] = {
  i8 97
  i8 98
  i8 99
  i8 0
}
global @readonly_embedded_nul [storage=readonly, binding=internal] = {
  i8 120
  i8 0
  i8 121
  i8 0
}
global @writable_word [binding=internal] = {
  i8 97
  i8 98
  i8 99
  i8 0
}
global @readonly_unterminated [storage=readonly, binding=internal] = {
  i8 97
  i8 98
}
global @escaped_table : ptr [binding=internal] = zero

function @measure_bytes(%data : ptr) -> i64
    [effects=readonly, unwind=no, binding=strong,
     object=cppgm_builtin_strlen, no_inline=yes] {
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

function @folds_complete_word() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_word
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @folds_at_first_nul() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_embedded_nul
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_writable_data() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @writable_word
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_unterminated_data() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_unterminated
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_dynamic_pointer(%data : ptr) -> i64 [binding=strong] {
  block ^entry:
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @folds_indexed_readonly_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @readonly_embedded_nul
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %first_byte = load u8 %data
    %first_byte64 = convert zext i64 u8 %first_byte
    %length = call i64 @measure_bytes(%data)
    %result = binary add i64 %length, %first_byte64
    return i64 %result
}

function @keeps_partial_readonly_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %first_data = addr @readonly_word
    store ptr %first_data, %table_base
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_writable_string_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @writable_word
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_escaped_readonly_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @readonly_embedded_nul
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    store ptr %table_base, @escaped_table
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_unterminated_string_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @readonly_unterminated
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_mutated_readonly_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @readonly_embedded_nul
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    store ptr %first_data, %second_slot
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_volatile_readonly_table(%which : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $table : obj<16x8>

  block ^entry:
    %table_base = addr $table
    %second_slot = index i8 %table_base, 8
    %first_data = addr @readonly_word
    %second_data = addr @readonly_embedded_nul
    store ptr %first_data, %table_base
    store ptr %second_data, %second_slot
    %element = index ptr [projection=array_element] %table_base, %which
    %data = load volatile ptr %element
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    %first = call i64 @folds_indexed_readonly_table(0)
    %second = call i64 @folds_indexed_readonly_table(1)
    %partial = call i64 @keeps_partial_readonly_table(0)
    %writable = call i64 @keeps_writable_string_table(1)
    %escaped = call i64 @keeps_escaped_readonly_table(1)
    %unterminated = call i64 @keeps_unterminated_string_table(0)
    %mutated = call i64 @keeps_mutated_readonly_table(1)
    %volatile = call i64 @keeps_volatile_readonly_table(1)
    %bad0 = cmp ne i64 %first, 100
    %bad1 = cmp ne i64 %second, 121
    %bad2 = cmp ne i64 %partial, 3
    %bad3 = cmp ne i64 %writable, 3
    %bad4 = cmp ne i64 %escaped, 1
    %bad5 = cmp ne i64 %unterminated, 3
    %bad6 = cmp ne i64 %mutated, 3
    %bad7 = cmp ne i64 %volatile, 1
    %bad01 = binary or i32 %bad0, %bad1
    %bad23 = binary or i32 %bad2, %bad3
    %bad0123 = binary or i32 %bad01, %bad23
    %bad45 = binary or i32 %bad4, %bad5
    %bad67 = binary or i32 %bad6, %bad7
    %bad4567 = binary or i32 %bad45, %bad67
    %bad = binary or i32 %bad0123, %bad4567
    return i32 %bad
}
