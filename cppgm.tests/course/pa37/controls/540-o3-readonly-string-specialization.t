global @letter [storage=readonly, binding=internal] = {
  i8 65
  i8 0
}
global @digit [storage=readonly, binding=internal] = {
  i8 48
  i8 0
}
global @high_byte [storage=readonly, binding=internal] = {
  i8 255
  i8 0
}
global @replaceable [storage=readonly, binding=strong] = {
  i8 65
  i8 0
}
global @writable [binding=internal] = {
  i8 48
  i8 0
}
global @unterminated [storage=readonly, binding=internal] = {
  i8 48
}

function @step(%value : i64) -> i64 [no_inline=yes, unwind=no] {
  block ^entry:
    %next = binary add i64 %value, 1
    return i64 %next
}

function @classify(%text : ptr, %bias : i64) -> i64
    [binding=internal, unwind=no] {
  block ^entry:
    %first = load i8 %text
    switch %first, ^fallback, 65:^letter, 48:^digit, -1:^high

  block ^letter:
    %letter_value = binary add i64 %bias, 10
    jump ^merge

  block ^digit:
    %digit_value = binary add i64 %bias, 20
    jump ^merge

  block ^high:
    %high_value = binary add i64 %bias, 30
    jump ^merge

  block ^fallback:
    %f0 = call i64 @step(%bias)
    %f1 = call i64 @step(%f0)
    %f2 = call i64 @step(%f1)
    %f3 = call i64 @step(%f2)
    %f4 = call i64 @step(%f3)
    %f5 = call i64 @step(%f4)
    %f6 = call i64 @step(%f5)
    %f7 = call i64 @step(%f6)
    %f8 = call i64 @step(%f7)
    %f9 = call i64 @step(%f8)
    %f10 = call i64 @step(%f9)
    %f11 = call i64 @step(%f10)
    %f12 = call i64 @step(%f11)
    %f13 = call i64 @step(%f12)
    %f14 = call i64 @step(%f13)
    %f15 = call i64 @step(%f14)
    %f16 = call i64 @step(%f15)
    %f17 = call i64 @step(%f16)
    %f18 = call i64 @step(%f17)
    %f19 = call i64 @step(%f18)
    %f20 = call i64 @step(%f19)
    %f21 = call i64 @step(%f20)
    %f22 = call i64 @step(%f21)
    %f23 = call i64 @step(%f22)
    %f24 = call i64 @step(%f23)
    %f25 = call i64 @step(%f24)
    %f26 = call i64 @step(%f25)
    %f27 = call i64 @step(%f26)
    %f28 = call i64 @step(%f27)
    %f29 = call i64 @step(%f28)
    %f30 = call i64 @step(%f29)
    %f31 = call i64 @step(%f30)
    %f32 = call i64 @step(%f31)
    %f33 = call i64 @step(%f32)
    %f34 = call i64 @step(%f33)
    %f35 = call i64 @step(%f34)
    %f36 = call i64 @step(%f35)
    %f37 = call i64 @step(%f36)
    %f38 = call i64 @step(%f37)
    %f39 = call i64 @step(%f38)
    %f40 = call i64 @step(%f39)
    %f41 = call i64 @step(%f40)
    %f42 = call i64 @step(%f41)
    %f43 = call i64 @step(%f42)
    %f44 = call i64 @step(%f43)
    %f45 = call i64 @step(%f44)
    %f46 = call i64 @step(%f45)
    %f47 = call i64 @step(%f46)
    jump ^merge

  block ^merge:
    %result = phi i64 [^letter: %letter_value, ^digit: %digit_value,
      ^high: %high_value, ^fallback: %f47]
    return i64 %result
}

function @volatile_probe(%text : ptr, %bias : i64) -> i64
    [binding=internal, unwind=no] {
  block ^entry:
    %first = load volatile i8 %text
    %v0 = call i64 @step(%bias)
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
    %wide = convert sext i64 i8 %first
    %result = binary add i64 %v39, %wide
    return i64 %result
}

function @indexed_probe(%text : ptr, %offset : i64, %bias : i64) -> i64
    [binding=internal, unwind=no] {
  block ^entry:
    %element = index i8 [projection=array_element] %text, %offset
    %byte = load u8 %element
    %v0 = call i64 @step(%bias)
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
    %wide = convert zext i64 u8 %byte
    %result = binary add i64 %v39, %wide
    return i64 %result
}

function @dynamic_calls(%text : ptr, %offset : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    %classified = call i64 @classify(%text, 7)
    %volatile = call i64 @volatile_probe(%text, 8)
    %indexed = call i64 @indexed_probe(%text, %offset, 9)
    %partial = binary add i64 %classified, %volatile
    %result = binary add i64 %partial, %indexed
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %letter_address = addr @letter
    %digit_address = addr @digit
    %high_address = addr @high_byte
    %replaceable_address = addr @replaceable
    %writable_address = addr @writable
    %unterminated_address = addr @unterminated
    %letter_result = call i64 @classify(%letter_address, 1)
    %digit_result = call i64 @classify(%digit_address, 2)
    %high_result = call i64 @classify(%high_address, 3)
    %replaceable_result = call i64 @classify(%replaceable_address, 4)
    %writable_result = call i64 @classify(%writable_address, 5)
    %unterminated_result = call i64 @classify(%unterminated_address, 6)
    %volatile_result = call i64 @volatile_probe(%letter_address, 8)
    %indexed_result = call i64 @indexed_probe(%letter_address, 1, 9)
    %dynamic_result = call i64 @dynamic_calls(%letter_address, 0)
    %s0 = binary add i64 %letter_result, %digit_result
    %s1 = binary add i64 %s0, %high_result
    %s2 = binary add i64 %s1, %replaceable_result
    %s3 = binary add i64 %s2, %writable_result
    %s4 = binary add i64 %s3, %unterminated_result
    %s5 = binary add i64 %s4, %volatile_result
    %s6 = binary add i64 %s5, %indexed_result
    %sum = binary add i64 %s6, %dynamic_result
    %bad = cmp ne i64 %sum, 537
    return i64 %bad
}
