function @explicit(%value : i64) -> i64
    [unwind=no, binding=strong, object=_Z8expliciti] {
  block ^entry:
    return i64 %value
}

function @inferred(%value : i64) -> i64
    [binding=strong, object=_Z8inferredi] {
  block ^entry:
    return i64 %value
}

function @caller(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    eh_try ^dispatch
    %explicit_result = call i64 @explicit(%value)
    %inferred_result = call i64 @inferred(%explicit_result)
    eh_end
    return i64 %inferred_result

  block ^dispatch:
    resume
}
