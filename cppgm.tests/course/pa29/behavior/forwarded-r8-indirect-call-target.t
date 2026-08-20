global @receiver : i8 = 0
global @source : i8 = 0
global @payload : i8 = 0
global @observed : i64 = 0
global @dispatch [storage=readonly] = {
  ptr addr @sink
}

function @sink(%this : ptr, %source : ptr, %type : i32, %data : ptr, %size : i64) -> void {
  block ^entry:
    store i64 %size, @observed
    return void
}

function @forward(%this : ptr, %source : ptr, %type : i32, %data : ptr, %size : i64) -> void {
  slot $this : ptr
  slot $source : ptr
  slot $type : i32
  slot $data : ptr
  slot $size : i64

  block ^entry:
    store ptr %this, $this
    store ptr %source, $source
    store i32 %type, $type
    store ptr %data, $data
    store i64 %size, $size
    %loaded_this = load ptr $this
    %loaded_source = load ptr $source
    %loaded_type = load i32 $type
    %loaded_data = load ptr $data
    %loaded_size = load i64 $size
    %table = addr @dispatch
    %target = load ptr %table
    call void %target(%loaded_this, %loaded_source, %loaded_type, %loaded_data, %loaded_size) as (%arg0 : ptr, %arg1 : ptr, %arg2 : i32, %arg3 : ptr, %arg4 : i64) -> void
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %this = addr @receiver
    %source = addr @source
    %data = addr @payload
    call void @forward(%this, %source, 13, %data, 77)
    %actual = load i64 @observed
    %wrong = cmp ne i64 %actual, 77
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
