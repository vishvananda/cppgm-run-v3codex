declare function @observe(%value : i64) -> void [unwind=no]

function @seven(%value : i64) -> void [unwind=no, binding=strong] {
  block ^entry:
    %v0 = binary add i64 %value, 0
    %v1 = binary add i64 %v0, 0
    %v2 = binary add i64 %v1, 0
    %v3 = binary add i64 %v2, 0
    %v4 = binary add i64 %v3, 0
    %v5 = binary add i64 %v4, 0
    %v6 = binary add i64 %v5, 0
    %v7 = binary add i64 %v6, 0
    %v8 = binary add i64 %v7, 0
    %v9 = binary add i64 %v8, 0
    %v10 = binary add i64 %v9, 0
    %v11 = binary add i64 %v10, 0
    %v12 = binary add i64 %v11, 0
    %v13 = binary add i64 %v12, 0
    %v14 = binary add i64 %v13, 0
    %v15 = binary add i64 %v14, 0
    %v16 = binary add i64 %v15, 0
    %v17 = binary add i64 %v16, 0
    %v18 = binary add i64 %v17, 0
    %v19 = binary add i64 %v18, 0
    %v20 = binary add i64 %v19, 0
    %v21 = binary add i64 %v20, 0
    %v22 = binary add i64 %v21, 0
    %v23 = binary add i64 %v22, 0
    %v24 = binary add i64 %v23, 0
    %v25 = binary add i64 %v24, 0
    %v26 = binary add i64 %v25, 0
    %v27 = binary add i64 %v26, 0
    %v28 = binary add i64 %v27, 0
    %v29 = binary add i64 %v28, 0
    %v30 = binary add i64 %v29, 0
    %v31 = binary add i64 %v30, 0
    %v32 = binary add i64 %v31, 0
    %v33 = binary add i64 %v32, 0
    %v34 = binary add i64 %v33, 0
    %v35 = binary add i64 %v34, 0
    %v36 = binary add i64 %v35, 0
    %v37 = binary add i64 %v36, 0
    %v38 = binary add i64 %v37, 0
    %v39 = binary add i64 %v38, 0
    call void @observe(%v39)
    call void @observe(%v39)
    call void @observe(%v39)
    call void @observe(%v39)
    call void @observe(%v39)
    call void @observe(%v39)
    return void
}

function @main(%value : i64) -> void [role=entry] {
  block ^entry:
    call void @seven(%value)
    return void
}
