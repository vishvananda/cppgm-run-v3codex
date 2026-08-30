global @size : i64 = 0
global @head : i64 = 1
global @line : i64 = 7
global @column : i64 = 9
global @value_sink : i64 = 0
global @line_sink : i64 = 0
global @column_sink : i64 = 0

function @next_value(%context : ptr) -> i64 [unwind=no] {
  block ^entry:
    return i64 5
}

function @allocate_marker() -> ptr [unwind=no] {
  block ^entry:
    %marker = addr @head
    return ptr %marker
}

function @touch_marker(%marker : ptr) -> void [unwind=no] {
  block ^entry:
    %value = load i64 %marker
    store i64 %value, @head
    return void
}

function @finish_marker(%marker : ptr) -> i64 [unwind=no] {
  block ^entry:
    %value = load i64 %marker
    return i64 %value
}

function @recolor_candidate(%context : ptr, %offset : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop_test

  block ^loop_test:
    %available = load i64 @size
    %needs_value = cmp ule i64 %available, %offset
    branch %needs_value, ^refill_prepare, ^done

  block ^refill_prepare:
    %available_again = load i64 @size
    %has_previous = cmp ne i64 %available_again, 0
    branch %has_previous, ^check_previous, ^refill

  block ^check_previous:
    %last = binary sub i64 %available_again, 1
    %head_for_check = load i64 @head
    %check_sum = binary add i64 %head_for_check, %last
    %check_wrapped = binary and i64 %check_sum, 3
    %check_scaled = binary mul i64 %check_wrapped, 24
    %is_sentinel = cmp eq i64 %check_scaled, -1
    branch %is_sentinel, ^early, ^refill

  block ^early:
    return i64 -1

  block ^refill:
    %value = call i64 @next_value(%context)
    %saved_line = load i64 @line
    %saved_column = load i64 @column
    %count = load i64 @size
    %full = cmp eq i64 %count, 4
    branch %full, ^cold, ^append

  block ^append:
    %current_head = load i64 @head
    %current_size = load i64 @size
    %sum = binary add i64 %current_head, %current_size
    %wrapped = binary and i64 %sum, 3
    %scaled = binary mul i64 %wrapped, 24
    store i64 %value, @value_sink
    store i64 %saved_line, @line_sink
    store i64 %saved_column, @column_sink
    %entry = index i8 [projection=array_element] %context, %scaled
    store i64 %value, %entry
    %line_entry = index i8 [projection=field] %entry, 8
    store i64 7, %line_entry
    %alternate_scaled = binary xor i64 %scaled, 24
    %alternate_entry = index i8 [projection=array_element] %context, %alternate_scaled
    %column_entry = index i8 [projection=field] %entry, 16
    store i64 9, %column_entry
    store i64 %value, %alternate_entry
    %current_size_again = load i64 @size
    %next_size = binary add i64 %current_size_again, 1
    store i64 %next_size, @size
    jump ^loop_test

  block ^done:
    %current_head_again = load i64 @head
    %selected = binary add i64 %current_head_again, %offset
    %wrapped_again = binary and i64 %selected, 3
    %scaled_again = binary mul i64 %wrapped_again, 24
    %entry_again = index i8 [projection=array_element] %context, %scaled_again
    %selected_value = load i64 %entry_again
    return i64 %selected_value

  block ^cold:
    %marker = call ptr @allocate_marker()
    call void @touch_marker(%marker)
    %cold_value = call i64 @finish_marker(%marker)
    return i64 %cold_value
}

function @cross_call_guard(%context : ptr, %offset : i64) -> i64 [unwind=no] {
  block ^entry:
    %before = load i64 @line
    %called = call i64 @next_value(%context)
    %after = call i64 @next_value(%context)
    %first = binary add i64 %before, %called
    %second = binary add i64 %first, %after
    %result = binary add i64 %second, %offset
    return i64 %result
}

function @even_save_recolor_candidate(%context : ptr, %offset : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop_test

  block ^loop_test:
    %available = load i64 @size
    %needs_value = cmp ule i64 %available, %offset
    branch %needs_value, ^refill_prepare, ^done

  block ^refill_prepare:
    %available_again = load i64 @size
    %has_previous = cmp ne i64 %available_again, 0
    branch %has_previous, ^check_previous, ^refill

  block ^check_previous:
    %last = binary sub i64 %available_again, 1
    %head_for_check = load i64 @head
    %check_sum = binary add i64 %head_for_check, %last
    %check_wrapped = binary and i64 %check_sum, 3
    %check_scaled = binary mul i64 %check_wrapped, 24
    %is_sentinel = cmp eq i64 %check_scaled, -1
    branch %is_sentinel, ^early, ^refill

  block ^early:
    return i64 -1

  block ^refill:
    %value = call i64 @next_value(%context)
    %saved_line = load i64 @line
    %saved_column = load i64 @column
    %count = load i64 @size
    %full = cmp eq i64 %count, 4
    branch %full, ^cold, ^append

  block ^append:
    %current_head = load i64 @head
    %current_size = load i64 @size
    %sum = binary add i64 %current_head, %current_size
    %wrapped = binary and i64 %sum, 3
    %scaled = binary mul i64 %wrapped, 24
    branch %offset, ^append_alternate, ^append_column

  block ^append_column:
    %entry = index i8 [projection=array_element] %context, %scaled
    store i64 %value, %entry
    %line_entry = index i8 [projection=field] %entry, 8
    store i64 %saved_line, %line_entry
    %column_entry = index i8 [projection=field] %entry, 16
    store i64 %saved_column, %column_entry
    jump ^append_finish

  block ^append_alternate:
    %alternate_entry = index i8 [projection=array_element] %context, %scaled
    store i64 %value, %alternate_entry
    %alternate_line = index i8 [projection=field] %alternate_entry, 8
    store i64 %saved_line, %alternate_line
    %alternate_column = index i8 [projection=field] %alternate_entry, 16
    store i64 %saved_column, %alternate_column
    store i64 %saved_column, @column_sink
    jump ^append_finish

  block ^append_finish:
    store i64 %value, @value_sink
    store i64 %saved_line, @line_sink
    store i64 %saved_column, @column_sink
    %current_size_again = load i64 @size
    %next_size = binary add i64 %current_size_again, 1
    store i64 %next_size, @size
    jump ^loop_test

  block ^done:
    %current_head_again = load i64 @head
    %selected = binary add i64 %current_head_again, %offset
    %wrapped_again = binary and i64 %selected, 3
    %scaled_again = binary mul i64 %wrapped_again, 24
    %entry_again = index i8 [projection=array_element] %context, %scaled_again
    %selected_value = load i64 %entry_again
    return i64 %selected_value

  block ^cold:
    %marker = call ptr @allocate_marker()
    call void @touch_marker(%marker)
    %cold_value = call i64 @finish_marker(%marker)
    return i64 %cold_value
}

function @main() -> i64 [role=entry, unwind=no] {
  slot $storage : obj<96x8>

  block ^entry:
    %context = addr $storage
    %candidate = call i64 @recolor_candidate(%context, 0)
    store i64 0, @size
    %even_candidate = call i64 @even_save_recolor_candidate(%context, 0)
    %guarded = call i64 @cross_call_guard(%context, 1)
    %value = load i64 @value_sink
    %saved_line = load i64 @line_sink
    %saved_column = load i64 @column_sink
    %bad0 = cmp ne i64 %candidate, 5
    %bad1 = cmp ne i64 %guarded, 18
    %bad2 = cmp ne i64 %value, 5
    %bad3 = cmp ne i64 %saved_line, 7
    %bad4 = cmp ne i64 %saved_column, 9
    %bad5 = cmp ne i64 %even_candidate, 5
    %bad01 = binary or i64 %bad0, %bad1
    %bad23 = binary or i64 %bad2, %bad3
    %bad0123 = binary or i64 %bad01, %bad23
    %bad04 = binary or i64 %bad0123, %bad4
    %bad = binary or i64 %bad04, %bad5
    return i64 %bad
}
