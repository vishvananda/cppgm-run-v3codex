global @effect_count : i64 = 0
global @constant_value : i64 [storage=readonly] = 7

function @__o2spec0(%value : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    return i64 %value
}

function @effect() -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %before = load i64 @effect_count
    %after = binary add i64 %before, 1
    store i64 %after, @effect_count
    return i64 %after
}

function @internal_target(
    %mode : i64, %value : i64, %unused : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    branch %mode, ^selected, ^other

  block ^selected:
    %result = binary add i64 %value, 1
    return i64 %result

  block ^other:
    %result2 = binary add i64 %value, 100
    return i64 %result2
}

function @weak_target(%mode : i64, %value : i64) -> i64
    [binding=weak, unwind=no] {
  block ^entry:
    branch %mode, ^selected, ^other

  block ^selected:
    %step0 = binary add i64 %value, 1
    %step1 = binary add i64 %step0, 1
    %step2 = binary add i64 %step1, 1
    %step3 = binary add i64 %step2, 1
    %step4 = binary add i64 %step3, 1
    %step5 = binary add i64 %step4, 1
    %step6 = binary add i64 %step5, 1
    %step7 = binary add i64 %step6, 1
    %step8 = binary add i64 %step7, 1
    %step9 = binary add i64 %step8, 1
    %step10 = binary add i64 %step9, 1
    %step11 = binary add i64 %step10, 1
    %step12 = binary add i64 %step11, 1
    %step13 = binary add i64 %step12, 1
    %step14 = binary add i64 %step13, 1
    %step15 = binary add i64 %step14, 1
    %step16 = binary add i64 %step15, 1
    %step17 = binary add i64 %step16, 1
    %step18 = binary add i64 %step17, 1
    %step19 = binary add i64 %step18, 1
    %step20 = binary add i64 %step19, 1
    %step21 = binary add i64 %step20, 1
    %step22 = binary add i64 %step21, 1
    %step23 = binary add i64 %step22, 1
    %step24 = binary add i64 %step23, 1
    %step25 = binary add i64 %step24, 1
    %step26 = binary add i64 %step25, 1
    %step27 = binary add i64 %step26, 1
    %step28 = binary add i64 %step27, 1
    %step29 = binary add i64 %step28, 1
    %step30 = binary add i64 %step29, 1
    %step31 = binary add i64 %step30, 1
    %step32 = binary add i64 %step31, 1
    %step33 = binary add i64 %step32, 1
    %step34 = binary add i64 %step33, 1
    %step35 = binary add i64 %step34, 1
    %step36 = binary add i64 %step35, 1
    %step37 = binary add i64 %step36, 1
    %step38 = binary add i64 %step37, 1
    %step39 = binary add i64 %step38, 1
    %step40 = binary add i64 %step39, 1
    return i64 %step40

  block ^other:
    %result2 = binary add i64 %value, 200
    return i64 %result2
}

function @read_constant(%address : ptr, %addend : i64) -> i64
    [binding=internal, no_inline=yes, effects=readonly, unwind=no] {
  block ^entry:
    %value = load i64 %address
    %result = binary add i64 %value, %addend
    return i64 %result
}

function @differing(%mode : i64, %value : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    branch %mode, ^selected, ^other

  block ^selected:
    return i64 %value

  block ^other:
    %negated = unary neg i64 %value
    return i64 %negated
}

function @addressed(%mode : i64, %value : i64) -> i64
    [binding=weak, no_inline=yes, unwind=no] {
  block ^entry:
    %result = binary add i64 %mode, %value
    return i64 %result
}

function @rooted(%mode : i64, %value : i64) -> i64
    [binding=weak, object_root=yes, no_inline=yes, unwind=no] {
  block ^entry:
    %result = binary add i64 %mode, %value
    return i64 %result
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %effect_value = call i64 @effect()
    %internal_a = call i64 @internal_target(1, 20, %effect_value)
    %internal_b = call i64 @internal_target(1, 30, 99)
    %weak_a = call i64 @weak_target(1, 40)
    %weak_b = call i64 @weak_target(1, 50)
    %constant_a = call i64 @read_constant(@constant_value, 1)
    %constant_b = call i64 @read_constant(@constant_value, 2)
    %different_a = call i64 @differing(0, 5)
    %different_b = call i64 @differing(1, 5)
    %address = addr @addressed
    %addressed_indirect = call i64 %address(2, 3)
      as (%mode : i64, %value : i64) -> i64 [unwind=no]
    %addressed_direct = call i64 @addressed(2, 4)
    %rooted_value = call i64 @rooted(3, 4)
    %sum1 = binary add i64 %internal_a, %internal_b
    %sum2 = binary add i64 %weak_a, %weak_b
    %sum3 = binary add i64 %different_a, %different_b
    %sum4 = binary add i64 %addressed_indirect, %addressed_direct
    %sum5 = binary add i64 %sum1, %sum2
    %sum6 = binary add i64 %sum3, %sum4
    %sum7 = binary add i64 %sum5, %sum6
    %sum8 = binary add i64 %constant_a, %constant_b
    %sum9 = binary add i64 %sum7, %sum8
    %result = binary add i64 %sum9, %rooted_value
    return i64 %result
}
