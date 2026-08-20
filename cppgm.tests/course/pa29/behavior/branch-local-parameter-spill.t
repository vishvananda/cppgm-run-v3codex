global @condition : i64 = 0
global @expected : i64 = 7
global @fields = {
  i64 0
  i64 0
  i64 0
}
global @late_result : i64 = 0
global @late_source : i64 = 13

function @noop() -> i64 {
  block ^entry:
    return i64 0
}

function @probe(%condition : ptr, %value : ptr) -> i64 {
  block ^entry:
    %take_pressure_path = load i64 %condition
    branch %take_pressure_path, ^pressure, ^merge

  block ^pressure:
    %a = copy i64 1
    %b = copy i64 2
    %c = copy i64 3
    %d = copy i64 4
    %e = copy i64 5
    %ignored = call i64 @noop()
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %abcd = binary add i64 %ab, %cd
    %all = binary add i64 %abcd, %e
    jump ^merge

  block ^merge:
    %result = load i64 %value
    return i64 %result
}

function @probe_temporary(%condition : ptr) -> i64 {
  block ^entry:
    %kept = copy i64 7
    %take_pressure_path = load i64 %condition
    branch %take_pressure_path, ^pressure, ^merge

  block ^pressure:
    %a = copy i64 1
    %b = copy i64 2
    %c = copy i64 3
    %d = copy i64 4
    %e = copy i64 5
    %ignored = call i64 @noop()
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %abcd = binary add i64 %ab, %cd
    %all = binary add i64 %abcd, %e
    jump ^merge

  block ^merge:
    return i64 %kept
}

function @probe_forwarded_alias(%base : ptr) -> i64 {
  block ^entry:
    %forwarded = index i8 %base, 0
    store i64 11, %forwarded
    %field1 = index i8 %base, 8
    store i64 22, %field1
    %field2 = index i8 %base, 16
    store i64 33, %field2
    %ignored = call i64 @noop()
    jump ^check

  block ^check:
    %first = load i64 %forwarded
    %second = load i64 %field1
    %third = load i64 %field2
    %first_ok = cmp eq i64 %first, 11
    %second_ok = cmp eq i64 %second, 22
    %third_ok = cmp eq i64 %third, 33
    %first_two = binary and i64 %first_ok, %second_ok
    %all = binary and i64 %first_two, %third_ok
    return i64 %all
}

function @clobber_first_argument(%value : ptr) -> i64 {
  block ^entry:
    %loaded = load i64 %value
    return i64 %loaded
}

function @write_after_call(%result : ptr [pass=indirect_result], %source : ptr) -> void {
  block ^entry:
    %ignored = call i64 @clobber_first_argument(%source)
    jump ^write

  block ^write:
    %field = index i8 %result, 0
    store i64 77, %field
    return void
}

function @dead_parameter_pressure(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> i64 {
  block ^entry:
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %abcd = binary add i64 %ab, %cd
    %sum = binary add i64 %abcd, %e
    %ignored = call i64 @noop()
    return i64 %sum
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %condition = addr @condition
    %expected = addr @expected
    %parameter = call i64 @probe(%condition, %expected)
    %temporary = call i64 @probe_temporary(%condition)
    %fields = addr @fields
    %alias_ok = call i64 @probe_forwarded_alias(%fields)
    %late_result = addr @late_result
    %late_source = addr @late_source
    call void @write_after_call(%late_result, %late_source)
    %late_value = load i64 %late_result
    %late_source_value = load i64 %late_source
    %pressure_sum = call i64 @dead_parameter_pressure(1, 2, 3, 4, 5)
    %wrong_parameter = cmp ne i64 %parameter, 7
    %wrong_temporary = cmp ne i64 %temporary, 7
    %wrong_alias = cmp eq i64 %alias_ok, 0
    %wrong_late_result = cmp ne i64 %late_value, 77
    %wrong_late_source = cmp ne i64 %late_source_value, 13
    %wrong_pressure = cmp ne i64 %pressure_sum, 15
    %wrong_spill = binary or i64 %wrong_parameter, %wrong_temporary
    %wrong_early = binary or i64 %wrong_spill, %wrong_alias
    %wrong_late = binary or i64 %wrong_late_result, %wrong_late_source
    %wrong_prior = binary or i64 %wrong_early, %wrong_late
    %wrong = binary or i64 %wrong_prior, %wrong_pressure
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
