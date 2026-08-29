global @cell : i64 = 1
global @condition : i64 = 0

function @shared_load_heavy(%seed : i64, %stop : i64) -> i64 [unwind=no] {
  block ^check0:
    %load0 = load i64 @cell
    %sum0 = binary add i64 %seed, %load0
    branch %stop, ^done0, ^check1
  block ^done0:
    return i64 %sum0
  block ^check1:
    %load1 = load i64 @cell
    %sum1 = binary add i64 %sum0, %load1
    branch %stop, ^done1, ^check2
  block ^done1:
    return i64 %sum1
  block ^check2:
    %load2 = load i64 @cell
    %sum2 = binary add i64 %sum1, %load2
    branch %stop, ^done2, ^check3
  block ^done2:
    return i64 %sum2
  block ^check3:
    %load3 = load i64 @cell
    %sum3 = binary add i64 %sum2, %load3
    branch %stop, ^done3, ^check4
  block ^done3:
    return i64 %sum3
  block ^check4:
    %load4 = load i64 @cell
    %sum4 = binary add i64 %sum3, %load4
    branch %stop, ^done4, ^check5
  block ^done4:
    return i64 %sum4
  block ^check5:
    %load5 = load i64 @cell
    %sum5 = binary add i64 %sum4, %load5
    branch %stop, ^done5, ^check6
  block ^done5:
    return i64 %sum5
  block ^check6:
    %load6 = load i64 @cell
    %sum6 = binary add i64 %sum5, %load6
    branch %stop, ^done6, ^check7
  block ^done6:
    return i64 %sum6
  block ^check7:
    %load7 = load i64 @cell
    %sum7 = binary add i64 %sum6, %load7
    branch %stop, ^done7, ^check8
  block ^done7:
    return i64 %sum7
  block ^check8:
    %load8 = load i64 @cell
    %sum8 = binary add i64 %sum7, %load8
    branch %stop, ^done8, ^check9
  block ^done8:
    return i64 %sum8
  block ^check9:
    %load9 = load i64 @cell
    %sum9 = binary add i64 %sum8, %load9
    branch %stop, ^done9, ^check10
  block ^done9:
    return i64 %sum9
  block ^check10:
    %load10 = load i64 @cell
    %sum10 = binary add i64 %sum9, %load10
    branch %stop, ^done10, ^check11
  block ^done10:
    return i64 %sum10
  block ^check11:
    %load11 = load i64 @cell
    %sum11 = binary add i64 %sum10, %load11
    branch %stop, ^done11, ^check12
  block ^done11:
    return i64 %sum11
  block ^check12:
    %load12 = load i64 @cell
    %sum12 = binary add i64 %sum11, %load12
    branch %stop, ^done12, ^check13
  block ^done12:
    return i64 %sum12
  block ^check13:
    %load13 = load i64 @cell
    %sum13 = binary add i64 %sum12, %load13
    branch %stop, ^done13, ^check14
  block ^done13:
    return i64 %sum13
  block ^check14:
    %load14 = load i64 @cell
    %sum14 = binary add i64 %sum13, %load14
    branch %stop, ^done14, ^check15
  block ^done14:
    return i64 %sum14
  block ^check15:
    %load15 = load i64 @cell
    %sum15 = binary add i64 %sum14, %load15
    branch %stop, ^done15, ^check16
  block ^done15:
    return i64 %sum15
  block ^check16:
    %load16 = load i64 @cell
    %sum16 = binary add i64 %sum15, %load16
    branch %stop, ^done16, ^check17
  block ^done16:
    return i64 %sum16
  block ^check17:
    %load17 = load i64 @cell
    %sum17 = binary add i64 %sum16, %load17
    branch %stop, ^done17, ^check18
  block ^done17:
    return i64 %sum17
  block ^check18:
    %load18 = load i64 @cell
    %sum18 = binary add i64 %sum17, %load18
    return i64 %sum18
}

function @store_barrier_guard() -> i64 [unwind=no] {
  block ^entry:
    %before = load i64 @cell
    store i64 7, @cell
    %after = load i64 @cell
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @caller_zero() -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %stop = load i64 @condition
    %result = call i64 @shared_load_heavy(0, %stop)
    return i64 %result
}

function @caller_one() -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %stop = load i64 @condition
    %result = call i64 @shared_load_heavy(1, %stop)
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %first = call i64 @caller_zero()
    %second = call i64 @caller_one()
    %bad0 = cmp ne i64 %first, 19
    %bad1 = cmp ne i64 %second, 20
    %bad = binary or i64 %bad0, %bad1
    return i64 %bad
}
