function @construct_left(%out : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    store i64 17, %out
    return void
}

function @construct_right(%out : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    store i64 23, %out
    return void
}

function @transfer(%out : ptr, %source : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 %source
    store i64 %value, %out
    return void
}

function @destroy(%object : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    return void
}

function @observe(%object : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    return void
}

function @eligible(%choose : i64) -> void [no_inline=yes] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    branch %choose, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    call void @transfer(%destination, %source) [elision=copy]
    call void @destroy(%source)
    return void
}

function @eh_eligible(%choose : i64) -> void [no_inline=yes] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    branch %choose, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    eh_try ^cleanup
    call void @transfer(%destination, %source) [elision=copy]
    call void @destroy(%source)
    eh_end
    jump ^done

  block ^cleanup:
    call void @destroy(%source)
    jump ^resume

  block ^done:
    return void

  block ^resume:
    resume
}

function @unmarked(%choose : i64) -> void [no_inline=yes] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    branch %choose, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    call void @transfer(%destination, %source)
    call void @destroy(%source)
    return void
}

function @escaping(%choose : i64) -> void [no_inline=yes] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    branch %choose, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    call void @observe(%source)
    call void @transfer(%destination, %source) [elision=copy]
    call void @destroy(%source)
    return void
}

function @observed_destination(%choose : i64) -> void [no_inline=yes] {
  slot $destination : i64
  slot $source : i64

  block ^entry:
    %destination = addr $destination
    %source = addr $source
    branch %choose, ^left, ^right

  block ^left:
    call void @construct_left(%source)
    call void @observe(%destination)
    jump ^merge

  block ^right:
    call void @construct_right(%source)
    jump ^merge

  block ^merge:
    call void @transfer(%destination, %source) [elision=copy]
    call void @destroy(%source)
    return void
}

function @main() -> i64 [role=entry] {
  block ^entry:
    call void @eligible(1)
    call void @eh_eligible(1)
    call void @unmarked(1)
    call void @escaping(1)
    call void @observed_destination(1)
    return i64 0
}
