global @left_value : i64 = 3
global @right_value : i64 = 5
global @constant_value : i64 [storage=readonly] = 7

declare function @readnone_observer() -> void [effects=readnone, unwind=no]
declare function @readonly_observer() -> void [effects=readonly, unwind=no]
declare function @unknown_observer() -> void

function @same_object() -> i64 {
  block ^entry:
    %address = addr @left_value
    %first = load i64 %address
    %second = load i64 %address
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @different_objects() -> i64 {
  block ^entry:
    %left_address = addr @left_value
    %right_address = addr @right_value
    %left = load i64 %left_address
    %right = load i64 %right_address
    %sum = binary add i64 %left, %right
    return i64 %sum
}

function @distinct_store() -> i64 {
  block ^entry:
    %left_address = addr @left_value
    %right_address = addr @right_value
    %before = load i64 %left_address
    store i64 9, %right_address
    %after = load i64 %left_address
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @same_store() -> i64 {
  block ^entry:
    %address = addr @left_value
    %before = load i64 %address
    store i64 9, %address
    %after = load i64 %address
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @call_effects() -> i64 {
  block ^entry:
    %address = addr @left_value
    %before_readnone = load i64 %address
    call void @readnone_observer()
    %after_readnone = load i64 %address
    call void @readonly_observer()
    %after_readonly = load i64 %address
    call void @unknown_observer()
    %after_unknown = load i64 %address
    %left_sum = binary add i64 %before_readnone, %after_readnone
    %right_sum = binary add i64 %after_readonly, %after_unknown
    %sum = binary add i64 %left_sum, %right_sum
    return i64 %sum
}

function @readonly_across_unknown_call() -> i64 {
  block ^entry:
    %address = addr @constant_value
    %before = load i64 %address
    call void @unknown_observer()
    %after = load i64 %address
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @branch_store(%choose : i64) -> i64 {
  block ^entry:
    %address = addr @left_value
    %before = load i64 %address
    branch %choose, ^write, ^keep

  block ^write:
    store i64 11, %address
    jump ^join

  block ^keep:
    jump ^join

  block ^join:
    %after = load i64 %address
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @same_unknown_pointer(%address : ptr) -> i64 {
  block ^entry:
    %first = load i64 %address
    %second = load i64 %address
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @possibly_aliasing_pointer(%left : ptr, %right : ptr) -> i64 {
  block ^entry:
    %before = load i64 %left
    store i64 9, %right
    %after = load i64 %left
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @disjoint_typed_projections() -> i64 {
  slot $pair : obj<16x8>

  block ^entry:
    %base = addr $pair
    %left = index i8 [projection=field] %base, 0
    %right = index i8 [projection=field] %base, 8
    store i64 3, %left
    %before = load i64 %left
    store i64 5, %right
    %after = load i64 %left
    %sum = binary add i64 %before, %after
    return i64 %sum
}
