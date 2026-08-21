function @late_candidate() -> i64 [binding=strong] {
  slot $s0 : obj<8x8>
  slot $s1 : obj<8x8>
  slot $s2 : obj<8x8>
  slot $s3 : obj<8x8>
  slot $s4 : obj<8x8>
  slot $s5 : obj<8x8>
  slot $s6 : obj<8x8>
  slot $s7 : obj<8x8>
  slot $s8 : obj<8x8>
  slot $s9 : obj<8x8>
  slot $s10 : obj<8x8>
  slot $s11 : obj<8x8>
  slot $s12 : obj<8x8>
  slot $s13 : obj<8x8>
  slot $s14 : obj<8x8>
  slot $s15 : obj<8x8>
  slot $s16 : obj<8x8>
  slot $s17 : obj<8x8>
  slot $s18 : obj<8x8>
  slot $s19 : obj<8x8>

  block ^entry:
    %a0 = addr $s0
    store i64 42, %a0
    %a1 = addr $s1
    copyobj 8x8 %a0, %a1
    %a2 = addr $s2
    copyobj 8x8 %a1, %a2
    %a3 = addr $s3
    copyobj 8x8 %a2, %a3
    %a4 = addr $s4
    copyobj 8x8 %a3, %a4
    %a5 = addr $s5
    copyobj 8x8 %a4, %a5
    %a6 = addr $s6
    copyobj 8x8 %a5, %a6
    %a7 = addr $s7
    copyobj 8x8 %a6, %a7
    %a8 = addr $s8
    copyobj 8x8 %a7, %a8
    %a9 = addr $s9
    copyobj 8x8 %a8, %a9
    %a10 = addr $s10
    copyobj 8x8 %a9, %a10
    %a11 = addr $s11
    copyobj 8x8 %a10, %a11
    %a12 = addr $s12
    copyobj 8x8 %a11, %a12
    %a13 = addr $s13
    copyobj 8x8 %a12, %a13
    %a14 = addr $s14
    copyobj 8x8 %a13, %a14
    %a15 = addr $s15
    copyobj 8x8 %a14, %a15
    %a16 = addr $s16
    copyobj 8x8 %a15, %a16
    %a17 = addr $s17
    copyobj 8x8 %a16, %a17
    %a18 = addr $s18
    copyobj 8x8 %a17, %a18
    %a19 = addr $s19
    copyobj 8x8 %a18, %a19
    %result = load i64 %a19
    return i64 %result
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %result = call i64 @late_candidate()
    return i64 %result
}
