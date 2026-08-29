function @step(%value : i64) -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %next = binary add i64 %value, 1
    return i64 %next
}

function @group_target(%mode : i64, %value : i64) -> i64
    [binding=internal, unwind=no] {
  block ^entry:
    %selected = cmp eq i64 %mode, 0
    branch %selected, ^small, ^large
  block ^small:
    return i64 %value
  block ^large:
    %v0 = call i64 @step(%value)
    %v1 = call i64 @step(%v0)
    %v2 = call i64 @step(%v1)
    %v3 = call i64 @step(%v2)
    %v4 = call i64 @step(%v3)
    %v5 = call i64 @step(%v4)
    %v6 = call i64 @step(%v5)
    %v7 = call i64 @step(%v6)
    %v8 = call i64 @step(%v7)
    %v9 = call i64 @step(%v8)
    %v10 = call i64 @step(%v9)
    %v11 = call i64 @step(%v10)
    %v12 = call i64 @step(%v11)
    %v13 = call i64 @step(%v12)
    %v14 = call i64 @step(%v13)
    %v15 = call i64 @step(%v14)
    %v16 = call i64 @step(%v15)
    %v17 = call i64 @step(%v16)
    %v18 = call i64 @step(%v17)
    %v19 = call i64 @step(%v18)
    %v20 = call i64 @step(%v19)
    %v21 = call i64 @step(%v20)
    %v22 = call i64 @step(%v21)
    %v23 = call i64 @step(%v22)
    %v24 = call i64 @step(%v23)
    %v25 = call i64 @step(%v24)
    %v26 = call i64 @step(%v25)
    %v27 = call i64 @step(%v26)
    %v28 = call i64 @step(%v27)
    %v29 = call i64 @step(%v28)
    %v30 = call i64 @step(%v29)
    %v31 = call i64 @step(%v30)
    %v32 = call i64 @step(%v31)
    %v33 = call i64 @step(%v32)
    %v34 = call i64 @step(%v33)
    %v35 = call i64 @step(%v34)
    %v36 = call i64 @step(%v35)
    %v37 = call i64 @step(%v36)
    %v38 = call i64 @step(%v37)
    %v39 = call i64 @step(%v38)
    %v40 = call i64 @step(%v39)
    return i64 %v40
}

function @edge_target(%value : i64) -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %at_zero = cmp ule i64 %value, 0
    branch %at_zero, ^zero_edge, ^nonzero
  block ^zero_edge:
    %redundant = cmp ne i64 %value, 0
    branch %redundant, ^bad, ^good
  block ^bad:
    return i64 1
  block ^good:
    return i64 0
  block ^nonzero:
    return i64 0
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %g0 = call i64 @group_target(0, 1)
    %g1 = call i64 @group_target(0, 2)
    %g2 = call i64 @group_target(0, 3)
    %g3 = call i64 @group_target(0, 4)
    %g4 = call i64 @group_target(0, 5)
    %g5 = call i64 @group_target(0, 6)
    %g6 = call i64 @group_target(0, 7)
    %g7 = call i64 @group_target(0, 8)
    %fallback = call i64 @group_target(1, 1)
    %edge = call i64 @edge_target(0)
    %s0 = binary add i64 %g0, %g1
    %s1 = binary add i64 %s0, %g2
    %s2 = binary add i64 %s1, %g3
    %s3 = binary add i64 %s2, %g4
    %s4 = binary add i64 %s3, %g5
    %s5 = binary add i64 %s4, %g6
    %s6 = binary add i64 %s5, %g7
    %s7 = binary add i64 %s6, %fallback
    %sum = binary add i64 %s7, %edge
    %bad = cmp ne i64 %sum, 78
    return i64 %bad
}
