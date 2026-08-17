global @values = {
  i64 17
  i64 19
  i64 23
  i64 29
  i64 31
  i64 37
  i64 41
}
global @observed_two : i64 = 0
global @observed_five : i64 = 0
global @observed_six : i64 = 0
global @observed_barrier : i64 = 0

function @distance_two(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %indexed = index i8 %base, 0
    %single = const i64 11
    %step1 = index i64 %indexed, 1
    %step2 = index i64 %step1, 1
    store i64 %single, @observed_two
    %loaded = load i64 %step2
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ac = binary add i64 %a, %c
    %bd = binary add i64 %b, %d
    %left = binary add i64 %ab, %cd
    %right = binary add i64 %ac, %bd
    %parameters = binary add i64 %left, %right
    %observed = load i64 @observed_two
    %fixed = binary add i64 %first, %second
    %values = binary add i64 %loaded, %observed
    %subtotal = binary add i64 %fixed, %values
    %result = binary add i64 %subtotal, %parameters
    return i64 %result
}

function @distance_five(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %indexed = index i8 %base, 0
    %single = const i64 11
    %step1 = index i64 %indexed, 1
    %step2 = index i64 %step1, 1
    %step3 = index i64 %step2, 1
    %step4 = index i64 %step3, 1
    %step5 = index i64 %step4, 1
    store i64 %single, @observed_five
    %loaded = load i64 %step5
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ac = binary add i64 %a, %c
    %bd = binary add i64 %b, %d
    %left = binary add i64 %ab, %cd
    %right = binary add i64 %ac, %bd
    %parameters = binary add i64 %left, %right
    %observed = load i64 @observed_five
    %fixed = binary add i64 %first, %second
    %values = binary add i64 %loaded, %observed
    %subtotal = binary add i64 %fixed, %values
    %result = binary add i64 %subtotal, %parameters
    return i64 %result
}

function @distance_six(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %indexed = index i8 %base, 0
    %single = const i64 11
    %step1 = index i64 %indexed, 1
    %step2 = index i64 %step1, 1
    %step3 = index i64 %step2, 1
    %step4 = index i64 %step3, 1
    %step5 = index i64 %step4, 1
    %step6 = index i64 %step5, 1
    store i64 %single, @observed_six
    %loaded = load i64 %step6
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ac = binary add i64 %a, %c
    %bd = binary add i64 %b, %d
    %left = binary add i64 %ab, %cd
    %right = binary add i64 %ac, %bd
    %parameters = binary add i64 %left, %right
    %observed = load i64 @observed_six
    %fixed = binary add i64 %first, %second
    %values = binary add i64 %loaded, %observed
    %subtotal = binary add i64 %fixed, %values
    %result = binary add i64 %subtotal, %parameters
    return i64 %result
}

function @source_redefinition_barrier(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %indexed = index i8 %base, 0
    %single = const i64 11
    %clobber = const i64 99
    store i64 %single, @observed_barrier
    %loaded = load i64 %indexed
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ac = binary add i64 %a, %c
    %bd = binary add i64 %b, %d
    %left = binary add i64 %ab, %cd
    %right = binary add i64 %ac, %bd
    %parameters = binary add i64 %left, %right
    %observed = load i64 @observed_barrier
    %fixed = binary add i64 %first, %second
    %values = binary add i64 %loaded, %observed
    %subtotal = binary add i64 %fixed, %values
    %result = binary add i64 %subtotal, %parameters
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @values
    %two = call i64 @distance_two(%base, 1, 2, 3, 4)
    %five = call i64 @distance_five(%base, 1, 2, 3, 4)
    %six = call i64 @distance_six(%base, 1, 2, 3, 4)
    %barrier = call i64 @source_redefinition_barrier(%base, 1, 2, 3, 4)
    %bad_two = cmp ne i64 %two, 354
    %bad_five = cmp ne i64 %five, 368
    %bad_six = cmp ne i64 %six, 372
    %bad_barrier = cmp ne i64 %barrier, 348
    %bad_a = binary or i64 %bad_two, %bad_five
    %bad_b = binary or i64 %bad_six, %bad_barrier
    %bad = binary or i64 %bad_a, %bad_b
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
