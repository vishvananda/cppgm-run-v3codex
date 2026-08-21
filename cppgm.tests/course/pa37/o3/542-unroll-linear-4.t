function @loop0() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header
  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit
  block ^body:
    %next = binary add i64 %i, 1
    jump ^header
  block ^exit:
    return i64 %i
}

function @loop1() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header
  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit
  block ^body:
    %next = binary add i64 %i, 1
    jump ^header
  block ^exit:
    return i64 %i
}

function @loop2() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header
  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit
  block ^body:
    %next = binary add i64 %i, 1
    jump ^header
  block ^exit:
    return i64 %i
}

function @loop3() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header
  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit
  block ^body:
    %next = binary add i64 %i, 1
    jump ^header
  block ^exit:
    return i64 %i
}
