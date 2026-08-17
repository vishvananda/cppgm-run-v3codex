global @value : u32 = 4294967295

function @barrier() -> void {
  block ^entry:
    return void
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
    %bad_global = cmp ne i64 %wide, 4294967295
    %bad_frame = cmp ne i64 %frame, 4294967295
    %bad = binary or i64 %bad_global, %bad_frame
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
