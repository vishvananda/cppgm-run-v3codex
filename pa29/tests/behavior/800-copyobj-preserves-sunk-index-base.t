global @cell = {
  i64 0
}

global @source = {
  i64 11
  i64 22
  i64 33
  i64 44
}

global @dest = {
  i64 0
  i64 0
  i64 0
  i64 0
  i64 0
}

function @mark(%p : ptr) -> void {
  block ^entry:
    store i64 99, %p
    return void
}

function @copy_then_mark_base(%this : ptr) -> void {
  slot $this : ptr

  block ^entry:
    store ptr %this, $this
    %base = load ptr $this
    %sub = index i8 %base, 0
    %src = addr @source
    %dst = addr @dest
    copyobj 24x8 %src, %dst
    call void @mark(%sub)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %cell = addr @cell
    call void @copy_then_mark_base(%cell)
    %v = load i64 @cell
    %ok = cmp eq i64 %v, 99
    %fail = cmp eq i64 %ok, 0
    %exit = convert trunc i32 i64 %fail
    return i32 %exit
}
