function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @copy_and_store_address(%condition : i64) -> i64 [unwind=no] {
  slot $value : i64
  slot $saved : ptr

  block ^entry:
    %address = addr $value
    %ignored = call i64 @identity(7)
    branch %condition, ^copy, ^direct

  block ^copy:
    %copied = copy ptr %address
    store ptr %copied, $saved
    jump ^done

  block ^direct:
    store ptr %address, $saved
    jump ^done

  block ^done:
    %observed = load ptr $saved
    %bad = cmp eq ptr %observed, 0
    return i64 %bad
}

function @return_slot_address() -> ptr [unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    %ignored = call i64 @identity(9)
    return ptr %address
}

function @integer_copy_arithmetic() -> i64 [unwind=no] {
  slot $value : i64

  block ^entry:
    %address = addr $value
    %integer = copy i64 %address
    %plus_one = binary add i64 %integer, 1
    %round_trip = binary sub i64 %plus_one, 1
    %bad = cmp ne i64 %round_trip, %integer
    return i64 %bad
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %copy_bad = call i64 @copy_and_store_address(1)
    %direct_bad = call i64 @copy_and_store_address(0)
    %returned = call ptr @return_slot_address()
    %return_bad = cmp eq ptr %returned, 0
    %arithmetic_bad = call i64 @integer_copy_arithmetic()
    %first = binary or i64 %copy_bad, %direct_bad
    %second = binary or i64 %first, %return_bad
    %result = binary or i64 %second, %arithmetic_bad
    return i64 %result
}
