global @state = {
  i64 0
  i64 0
  i64 0
  i64 0
}
global @arena = {
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
}
global @stats : i64 = 0
global @result : i64 = 0
global @node = {
  i64 0
  i64 0
  i64 0
  i64 0
}
global @edges = {
  u32 0
  u32 1
  u32 0
  u32 4294967295
}
global @no_edge : u32 = 4294967295

function @edge_at(%base : ptr, %index : i64) -> ptr [unwind=no, no_inline=yes] {
  block ^entry:
    %offset = binary mul i64 %index, 8
    %result = index i8 @edges, %offset
    return ptr %result
}

function @node_at(%base : ptr, %index : i64) -> ptr [unwind=no, no_inline=yes] {
  block ^entry:
    %result = addr @node
    return ptr %result
}

function @payload(%arena_pointer : ptr, %value : u32) -> u32 [unwind=no, no_inline=yes] {
  block ^entry:
    return u32 %value
}

function @append(%destination : ptr, %value : ptr) -> void [unwind=no, no_inline=yes] {
  block ^entry:
    return void
}

function @walk(%this : ptr, %declarator : u32, %fallback : u32, %names : ptr) -> void [unwind=no, no_inline=yes] {
  slot $fallback : u32
  slot $payload : u32

  block ^entry:
    store u32 %fallback, $fallback
    %stable_arena_slot = index i8 [projection=field] %this, 24
    %arena_0 = load ptr %stable_arena_slot
    %nodes_0 = index i8 [projection=field] %arena_0, 32
    %declarator_index = convert zext i64 u32 %declarator
    %node_0 = call ptr @node_at(%nodes_0, %declarator_index)
    %first_edge_slot = index i8 [projection=field] %node_0, 12
    %first_edge = load u32 %first_edge_slot
    jump ^outer_condition

  block ^outer_condition:
    %edge = phi u32 [^entry: %first_edge, ^outer_iterate: %next_edge]
    %sentinel = load u32 @no_edge
    %more = cmp ne u32 %edge, %sentinel
    branch %more, ^outer_body, ^outer_done

  block ^outer_body:
    %arena_1 = load ptr %stable_arena_slot
    %edges_1 = index i8 [projection=field] %arena_1, 56
    %wide_edge = convert zext i64 u32 %edge
    %edge_record_1 = call ptr @edge_at(%edges_1, %wide_edge)
    %child_slot = index i8 [projection=field] %edge_record_1, 0
    %child = load u32 %child_slot
    %arena_2 = load ptr %stable_arena_slot
    %stats_slot = index i8 [projection=field] %arena_2, 8
    %stats_pointer = load ptr %stats_slot
    branch %stats_pointer, ^record, ^inspect

  block ^record:
    %count = load i64 %stats_pointer
    %next_count = binary add i64 %count, 1
    store i64 %next_count, %stats_pointer
    jump ^inspect

  block ^inspect:
    %nodes_1 = index i8 [projection=field] %arena_2, 32
    %child_index = convert zext i64 u32 %child
    %node_1 = call ptr @node_at(%nodes_1, %child_index)
    %kind_slot = index i8 [projection=field] %node_1, 30
    %kind = load u16 %kind_slot
    %is_target = cmp eq u16 %kind, 121
    %not_target = cmp eq u8 %is_target, 0
    branch %not_target, ^outer_iterate, ^inner_start

  block ^inner_start:
    %arena_3 = load ptr %stable_arena_slot
    %nodes_2 = index i8 [projection=field] %arena_3, 32
    %node_2 = call ptr @node_at(%nodes_2, %child_index)
    %inner_first_slot = index i8 [projection=field] %node_2, 12
    %inner_first = load u32 %inner_first_slot
    jump ^inner_condition

  block ^inner_condition:
    %inner_edge = phi u32 [^inner_start: %inner_first, ^inner_body: %inner_next]
    %inner_sentinel = load u32 @no_edge
    %inner_more = cmp ne u32 %inner_edge, %inner_sentinel
    branch %inner_more, ^inner_body, ^inner_done

  block ^inner_body:
    %inner_arena_1 = load ptr %stable_arena_slot
    %inner_edges_1 = index i8 [projection=field] %inner_arena_1, 56
    %inner_wide = convert zext i64 u32 %inner_edge
    %inner_record_1 = call ptr @edge_at(%inner_edges_1, %inner_wide)
    %inner_child_slot = index i8 [projection=field] %inner_record_1, 0
    %inner_child = load u32 %inner_child_slot
    %inner_value = call u32 @payload(%inner_arena_1, %inner_child)
    store u32 %inner_value, $payload
    %payload_address = addr $payload
    call void @append(%names, %payload_address)
    %inner_arena_2 = load ptr %stable_arena_slot
    %inner_edges_2 = index i8 [projection=field] %inner_arena_2, 56
    %inner_record_2 = call ptr @edge_at(%inner_edges_2, %inner_wide)
    %inner_next_slot = index i8 [projection=field] %inner_record_2, 4
    %inner_next = load u32 %inner_next_slot
    jump ^inner_condition

  block ^inner_done:
    return void

  block ^outer_iterate:
    %arena_4 = load ptr %stable_arena_slot
    %edges_3 = index i8 [projection=field] %arena_4, 56
    %edge_record_2 = call ptr @edge_at(%edges_3, %wide_edge)
    %next_edge_slot = index i8 [projection=field] %edge_record_2, 4
    %next_edge = load u32 %next_edge_slot
    jump ^outer_condition

  block ^outer_done:
    %fallback_address = addr $fallback
    call void @append(%names, %fallback_address)
    return void
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %arena_slot = index i8 [projection=field] @state, 24
    store ptr @arena, %arena_slot
    %stats_slot = index i8 [projection=field] @arena, 8
    store ptr @stats, %stats_slot
    call void @walk(@state, 0, 0, @result)
    %visits = load i64 @stats
    %bad = cmp ne i64 %visits, 2
    return i64 %bad
}
