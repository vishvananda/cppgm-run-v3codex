function @main(%x : i64) -> i64 [role=entry] {
  block ^entry:
    %result = call i64 @outer(%x)
    %address = addr @addressed
    %kept = call i64 %address(%result) as (%arg0 : i64) -> i64
    return i64 %kept
}

function @outer(%x : i64) -> i64 [binding=weak] {
  block ^entry:
    %result = call i64 @inner(%x)
    return i64 %result
}

function @inner(%x : i64) -> i64 [binding=weak] {
  block ^entry:
    %result = binary add i64 %x, 1
    return i64 %result
}

function @addressed(%x : i64) -> i64 [binding=weak] {
  block ^entry:
    return i64 %x
}

function @rooted() -> i64 [binding=weak, object_root=yes] {
  block ^entry:
    return i64 7
}
