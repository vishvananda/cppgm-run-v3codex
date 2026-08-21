global @data = {
  i64 0
  i64 0
}
global @a : i64 = 1
global @b : i64 = 2
global @c : i64 = 3
global @d : i64 = 4

function @noop() -> i64 [unwind=no] {
  block ^entry:
    return i64 0
}

function @probe() -> i64 [unwind=no] {
  slot $counter : i64

  block ^entry:
    %base = addr @data
    %a = load i64 @a
    %b = load i64 @b
    %c = load i64 @c
    %d = load i64 @d
    store i64 0, $counter
    jump ^condition

  block ^condition:
    %iteration = load i64 $counter
    %more = cmp ult i64 %iteration, 2
    branch %more, ^body, ^done

  block ^body:
    %address = index i64 %base, %iteration
    store i64 %iteration, %address
    %later = copy i64 7
    %ignored = call i64 @noop()
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %prior = binary add i64 %ab, %cd
    %all = binary add i64 %prior, %later
    %next = binary add i64 %iteration, 1
    store i64 %next, $counter
    %keep_live = cmp eq i64 %all, 17
    branch %keep_live, ^condition, ^failure

  block ^done:
    %second = index i64 %base, 1
    %value = load i64 %second
    %bad = cmp ne i64 %value, 1
    return i64 %bad

  block ^failure:
    return i64 1
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %result = call i64 @probe()
    return i64 %result
}
