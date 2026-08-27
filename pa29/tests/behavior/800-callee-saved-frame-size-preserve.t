global @cell = {
  i64 9
}

function @identity(%p : ptr) -> ptr {
  block ^entry:
    return ptr %p
}

function @probe(%this : ptr, %other : ptr [pass=by_address], %tag : obj<1x1>) -> ptr {
  slot $tag : obj<1x1>

  block ^entry:
    %tag_addr = addr $tag
    copyobj 1x1 %tag, %tag_addr
    %a = call ptr @identity(%this)
    %b = call ptr @identity(%other)
    %c = call ptr @identity(%b)
    return ptr %c
}

function @main() -> i64 [role=entry] {
  slot $tag : obj<1x1>

  block ^entry:
    %p = addr @cell
    %tag_addr = addr $tag
    zeroinit 1x1 %tag_addr
    %r = call ptr @probe(%p, %p, $tag)
    %wrong = cmp ne ptr %r, %p
    branch %wrong, ^bad, ^check

  block ^check:
    %v = load i64 %r
    %bad_value = cmp ne i64 %v, 9
    return i64 %bad_value

  block ^bad:
    return i64 2
}
