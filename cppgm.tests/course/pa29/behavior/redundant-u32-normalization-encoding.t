global @value : u32 = 4294967295

function @barrier() -> void {
  block ^entry:
    return void
}

function @return_u32() -> u32 {
  block ^entry:
    %value = copy u32 4294967295
    return u32 %value
}

function @load_frame() -> i64 {
  slot $home : u32

  block ^entry:
    %value = copy u32 4294967295
    store u32 %value, $home
    call void @barrier()
    %loaded = load u32 $home
    %wide = copy i64 %loaded
    return i64 %wide
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %loaded = load u32 @value
    %wide = copy i64 %loaded
    %frame = call i64 @load_frame()
    %called = call u32 @return_u32()
    %called_wide = copy i64 %called
    %bad_global = cmp ne i64 %wide, 4294967295
    %bad_frame = cmp ne i64 %frame, 4294967295
    %bad_call = cmp ne i64 %called_wide, 4294967295
    %bad_loads = binary or i64 %bad_global, %bad_frame
    %bad = binary or i64 %bad_loads, %bad_call
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
