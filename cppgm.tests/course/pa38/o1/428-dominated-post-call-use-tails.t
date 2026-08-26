function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @post_call_tails() -> i64 {
  block ^entry:
    eh_try ^handler
    %a = call i64 @identity(1)
    %b = call i64 @identity(2)
    %c = call i64 @identity(3)
    %d = call i64 @identity(4)
    %e = call i64 @identity(5)
    %v = call i64 @identity(40)
    %w = call i64 @identity(50)
    %x = call i64 @identity(60)
    %y = call i64 @identity(70)
    %ignored = call i64 @identity(9)
    eh_end
    jump ^tail

  block ^tail:
    %a2 = binary add i64 %a, %a
    %a3 = binary add i64 %a2, %a
    %b2 = binary add i64 %b, %b
    %b3 = binary add i64 %b2, %b
    %c2 = binary add i64 %c, %c
    %c3 = binary add i64 %c2, %c
    %d2 = binary add i64 %d, %d
    %d3 = binary add i64 %d2, %d
    %e2 = binary add i64 %e, %e
    %e3 = binary add i64 %e2, %e
    %v0 = binary add i64 %v, 1
    %w0 = binary add i64 %w, 1
    %x0 = binary add i64 %x, 1
    %y0 = binary add i64 %y, 1
    %v1 = binary add i64 %v, 2
    %w1 = binary add i64 %w, 2
    %x1 = binary add i64 %x, 2
    %y1 = binary add i64 %y, 2
    %kept0 = binary add i64 %a3, %b3
    %kept1 = binary add i64 %c3, %d3
    %kept2 = binary add i64 %kept0, %kept1
    %kept = binary add i64 %kept2, %e3
    %tail0 = binary add i64 %v0, %v1
    %tail1 = binary add i64 %w0, %w1
    %tail2 = binary add i64 %x0, %x1
    %tail3 = binary add i64 %y0, %y1
    %tails0 = binary add i64 %tail0, %tail1
    %tails1 = binary add i64 %tail2, %tail3
    %tails = binary add i64 %tails0, %tails1
    %result = binary add i64 %kept, %tails
    return i64 %result

  block ^handler:
    return i64 99
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %result = call i64 @post_call_tails()
    %bad = cmp ne i64 %result, 497
    return i64 %bad
}
