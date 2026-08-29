function @small_alignment_control(%input : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    %result = binary add i64 %input, 1
    return i64 %result
}

function @large_alignment_candidate(%input : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  slot $observed : i64

  block ^entry:
    store volatile i64 %input, $observed
    %sample1 = load volatile i64 $observed
    %sample2 = load volatile i64 $observed
    %sum2 = binary add i64 %sample1, %sample2
    %sample3 = load volatile i64 $observed
    %sum3 = binary add i64 %sum2, %sample3
    %sample4 = load volatile i64 $observed
    %sum4 = binary add i64 %sum3, %sample4
    %sample5 = load volatile i64 $observed
    %sum5 = binary add i64 %sum4, %sample5
    %sample6 = load volatile i64 $observed
    %sum6 = binary add i64 %sum5, %sample6
    %sample7 = load volatile i64 $observed
    %sum7 = binary add i64 %sum6, %sample7
    %sample8 = load volatile i64 $observed
    %sum8 = binary add i64 %sum7, %sample8
    %sample9 = load volatile i64 $observed
    %sum9 = binary add i64 %sum8, %sample9
    %sample10 = load volatile i64 $observed
    %sum10 = binary add i64 %sum9, %sample10
    %sample11 = load volatile i64 $observed
    %sum11 = binary add i64 %sum10, %sample11
    %sample12 = load volatile i64 $observed
    %sum12 = binary add i64 %sum11, %sample12
    %sample13 = load volatile i64 $observed
    %sum13 = binary add i64 %sum12, %sample13
    %sample14 = load volatile i64 $observed
    %sum14 = binary add i64 %sum13, %sample14
    %sample15 = load volatile i64 $observed
    %sum15 = binary add i64 %sum14, %sample15
    %sample16 = load volatile i64 $observed
    %sum16 = binary add i64 %sum15, %sample16
    %sample17 = load volatile i64 $observed
    %sum17 = binary add i64 %sum16, %sample17
    %sample18 = load volatile i64 $observed
    %sum18 = binary add i64 %sum17, %sample18
    %sample19 = load volatile i64 $observed
    %sum19 = binary add i64 %sum18, %sample19
    %sample20 = load volatile i64 $observed
    %sum20 = binary add i64 %sum19, %sample20
    %sample21 = load volatile i64 $observed
    %sum21 = binary add i64 %sum20, %sample21
    %sample22 = load volatile i64 $observed
    %sum22 = binary add i64 %sum21, %sample22
    %sample23 = load volatile i64 $observed
    %sum23 = binary add i64 %sum22, %sample23
    %sample24 = load volatile i64 $observed
    %sum24 = binary add i64 %sum23, %sample24
    %sample25 = load volatile i64 $observed
    %sum25 = binary add i64 %sum24, %sample25
    %sample26 = load volatile i64 $observed
    %sum26 = binary add i64 %sum25, %sample26
    %sample27 = load volatile i64 $observed
    %sum27 = binary add i64 %sum26, %sample27
    %sample28 = load volatile i64 $observed
    %sum28 = binary add i64 %sum27, %sample28
    %sample29 = load volatile i64 $observed
    %sum29 = binary add i64 %sum28, %sample29
    %sample30 = load volatile i64 $observed
    %sum30 = binary add i64 %sum29, %sample30
    %sample31 = load volatile i64 $observed
    %sum31 = binary add i64 %sum30, %sample31
    %sample32 = load volatile i64 $observed
    %sum32 = binary add i64 %sum31, %sample32
    %sample33 = load volatile i64 $observed
    %sum33 = binary add i64 %sum32, %sample33
    %sample34 = load volatile i64 $observed
    %sum34 = binary add i64 %sum33, %sample34
    %sample35 = load volatile i64 $observed
    %sum35 = binary add i64 %sum34, %sample35
    %sample36 = load volatile i64 $observed
    %sum36 = binary add i64 %sum35, %sample36
    %sample37 = load volatile i64 $observed
    %sum37 = binary add i64 %sum36, %sample37
    %sample38 = load volatile i64 $observed
    %sum38 = binary add i64 %sum37, %sample38
    %sample39 = load volatile i64 $observed
    %sum39 = binary add i64 %sum38, %sample39
    %sample40 = load volatile i64 $observed
    %sum40 = binary add i64 %sum39, %sample40
    return i64 %sum40
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %large = call i64 @large_alignment_candidate(1)
    %small = call i64 @small_alignment_control(5)
    %sum = binary add i64 %large, %small
    %bad = cmp ne i64 %sum, 46
    return i64 %bad
}
