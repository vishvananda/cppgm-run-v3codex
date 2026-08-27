declare function @bad() -> void

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %result = call i64 @wrap7()
    return i64 %result
}

function @other() -> i64 [binding=strong] {
  block ^entry:
    %result = call i64 @wrap7()
    return i64 %result
}

function @wrap7() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap6()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap6() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap5()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap5() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap4()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap4() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap3()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap3() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap2()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap2() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap1()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap1() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @wrap0()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @wrap0() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @leaf()
    branch %value, ^taken, ^dead

  block ^taken:
    jump ^join

  block ^dead:
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    call void @bad()
    jump ^join

  block ^join:
    return i64 1
}

function @leaf() -> i64 [binding=strong] {
  block ^entry:
    return i64 1
}
