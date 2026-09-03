function @construct_left(%out : ptr) -> void {
  block ^entry:
    store i64 17, %out
    return void
}

function @construct_right(%out : ptr) -> void {
  block ^entry:
    store i64 23, %out
    return void
}

function @transfer(%out : ptr, %source : ptr) -> void {
  block ^entry:
    %value = load i64 %source
    store i64 %value, %out
    return void
}

function @destroy(%object : ptr) -> void {
  block ^entry:
    return void
}

function @main() -> i64 [role=entry] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    %choose_left = cmp eq i64 1, 1
    branch %choose_left, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    call void @transfer(%destination, %source) [elision=copy]
    call void @destroy(%source)
    %actual = load i64 %destination
    %wrong = cmp ne i64 %actual, 17
    return i64 %wrong
}
