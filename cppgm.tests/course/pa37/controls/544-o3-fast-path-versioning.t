global @seen : i64 [binding=internal] = 0
global @written : i64 [binding=internal] = 0

function @observe(%value : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store volatile i64 %value, @seen
    return void
}

function @versionable(%value : i64, %choose : i64) -> void
    [binding=internal, unwind=no] {
  block ^entry:
    %take_left = cmp ne i64 %choose, 0
    branch %take_left, ^left, ^right

  block ^left:
    %left_value = binary add i64 %value, 1
    jump ^join

  block ^right:
    %right_value = binary sub i64 %value, 1
    jump ^join

  block ^join:
    %selected = phi i64 [^left: %left_value, ^right: %right_value]
    jump ^check0

  block ^check0:
    %match0 = cmp eq i64 %selected, 0
    branch %match0, ^slow0, ^check1

  block ^check1:
    %match1 = cmp eq i64 %value, 1
    branch %match1, ^slow1, ^check2

  block ^check2:
    %match2 = cmp eq i64 %value, 2
    branch %match2, ^slow2, ^check3

  block ^check3:
    %match3 = cmp eq i64 %value, 3
    branch %match3, ^slow3, ^check4

  block ^check4:
    %match4 = cmp eq i64 %value, 4
    branch %match4, ^slow0, ^check5

  block ^check5:
    %match5 = cmp eq i64 %value, 5
    branch %match5, ^slow1, ^check6

  block ^check6:
    %match6 = cmp eq i64 %value, 6
    branch %match6, ^slow2, ^check7

  block ^check7:
    %match7 = cmp eq i64 %value, 7
    branch %match7, ^slow3, ^check8

  block ^check8:
    %match8 = cmp eq i64 %value, 8
    branch %match8, ^slow0, ^check9

  block ^check9:
    %match9 = cmp eq i64 %value, 9
    branch %match9, ^slow1, ^check10

  block ^check10:
    %match10 = cmp eq i64 %value, 10
    branch %match10, ^slow2, ^check11

  block ^check11:
    %match11 = cmp eq i64 %value, 11
    branch %match11, ^slow3, ^check12

  block ^check12:
    %match12 = cmp eq i64 %value, 12
    branch %match12, ^slow0, ^check13

  block ^check13:
    %match13 = cmp eq i64 %value, 13
    branch %match13, ^slow1, ^check14

  block ^check14:
    %match14 = cmp eq i64 %value, 14
    branch %match14, ^slow2, ^check15

  block ^check15:
    %match15 = cmp eq i64 %value, 15
    branch %match15, ^slow3, ^check16

  block ^check16:
    %match16 = cmp eq i64 %value, 16
    branch %match16, ^slow0, ^check17

  block ^check17:
    %match17 = cmp eq i64 %value, 17
    branch %match17, ^slow1, ^check18

  block ^check18:
    %match18 = cmp eq i64 %value, 18
    branch %match18, ^slow2, ^check19

  block ^check19:
    %match19 = cmp eq i64 %value, 19
    branch %match19, ^slow3, ^check20

  block ^check20:
    %match20 = cmp eq i64 %value, 20
    branch %match20, ^slow0, ^check21

  block ^check21:
    %match21 = cmp eq i64 %value, 21
    branch %match21, ^slow1, ^check22

  block ^check22:
    %match22 = cmp eq i64 %value, 22
    branch %match22, ^slow2, ^check23

  block ^check23:
    %match23 = cmp eq i64 %value, 23
    branch %match23, ^slow3, ^check24

  block ^check24:
    %match24 = cmp eq i64 %value, 24
    branch %match24, ^slow0, ^check25

  block ^check25:
    %match25 = cmp eq i64 %value, 25
    branch %match25, ^slow1, ^check26

  block ^check26:
    %match26 = cmp eq i64 %value, 26
    branch %match26, ^slow2, ^check27

  block ^check27:
    %match27 = cmp eq i64 %value, 27
    branch %match27, ^slow3, ^check28

  block ^check28:
    %match28 = cmp eq i64 %value, 28
    branch %match28, ^slow0, ^check29

  block ^check29:
    %match29 = cmp eq i64 %value, 29
    branch %match29, ^slow1, ^check30

  block ^check30:
    %match30 = cmp eq i64 %value, 30
    branch %match30, ^slow2, ^check31

  block ^check31:
    %match31 = cmp eq i64 %value, 31
    branch %match31, ^slow3, ^check32

  block ^check32:
    %match32 = cmp eq i64 %value, 32
    branch %match32, ^slow0, ^check33

  block ^check33:
    %match33 = cmp eq i64 %value, 33
    branch %match33, ^slow1, ^check34

  block ^check34:
    %match34 = cmp eq i64 %value, 34
    branch %match34, ^slow2, ^check35

  block ^check35:
    %match35 = cmp eq i64 %value, 35
    branch %match35, ^slow3, ^check36

  block ^check36:
    %match36 = cmp eq i64 %value, 36
    branch %match36, ^slow0, ^check37

  block ^check37:
    %match37 = cmp eq i64 %value, 37
    branch %match37, ^slow1, ^check38

  block ^check38:
    %match38 = cmp eq i64 %value, 38
    branch %match38, ^slow2, ^check39

  block ^check39:
    %match39 = cmp eq i64 %value, 39
    branch %match39, ^slow3, ^check40

  block ^check40:
    %match40 = cmp eq i64 %value, 40
    branch %match40, ^slow0, ^check41

  block ^check41:
    %match41 = cmp eq i64 %value, 41
    branch %match41, ^slow1, ^check42

  block ^check42:
    %match42 = cmp eq i64 %value, 42
    branch %match42, ^slow2, ^check43

  block ^check43:
    %match43 = cmp eq i64 %value, 43
    branch %match43, ^slow3, ^check44

  block ^check44:
    %match44 = cmp eq i64 %value, 44
    branch %match44, ^slow0, ^check45

  block ^check45:
    %match45 = cmp eq i64 %value, 45
    branch %match45, ^slow1, ^check46

  block ^check46:
    %match46 = cmp eq i64 %value, 46
    branch %match46, ^slow2, ^check47

  block ^check47:
    %match47 = cmp eq i64 %value, 47
    branch %match47, ^slow3, ^check48

  block ^check48:
    %match48 = cmp eq i64 %value, 48
    branch %match48, ^slow0, ^check49

  block ^check49:
    %match49 = cmp eq i64 %value, 49
    branch %match49, ^slow1, ^check50

  block ^check50:
    %match50 = cmp eq i64 %value, 50
    branch %match50, ^slow2, ^check51

  block ^check51:
    %match51 = cmp eq i64 %value, 51
    branch %match51, ^slow3, ^check52

  block ^check52:
    %match52 = cmp eq i64 %value, 52
    branch %match52, ^slow0, ^check53

  block ^check53:
    %match53 = cmp eq i64 %value, 53
    branch %match53, ^slow1, ^check54

  block ^check54:
    %match54 = cmp eq i64 %value, 54
    branch %match54, ^slow2, ^check55

  block ^check55:
    %match55 = cmp eq i64 %value, 55
    branch %match55, ^slow3, ^check56

  block ^check56:
    %match56 = cmp eq i64 %value, 56
    branch %match56, ^slow0, ^check57

  block ^check57:
    %match57 = cmp eq i64 %value, 57
    branch %match57, ^slow1, ^fast

  block ^fast:
    return void

  block ^slow0:
    call void @observe(%value)
    return void

  block ^slow1:
    call void @observe(%value)
    return void

  block ^slow2:
    call void @observe(%value)
    return void

  block ^slow3:
    call void @observe(%value)
    return void
}

function @effect_before_bailout(%value : i64, %out : ptr) -> void
    [binding=internal, unwind=no] {
  block ^check0:
    store i64 %value, %out
    %unsafe_match0 = cmp eq i64 %value, 0
    branch %unsafe_match0, ^unsafe_slow0, ^unsafe_check1

  block ^unsafe_check1:
    %unsafe_match1 = cmp eq i64 %value, 1
    branch %unsafe_match1, ^unsafe_slow1, ^unsafe_check2

  block ^unsafe_check2:
    %unsafe_match2 = cmp eq i64 %value, 2
    branch %unsafe_match2, ^unsafe_slow2, ^unsafe_check3

  block ^unsafe_check3:
    %unsafe_match3 = cmp eq i64 %value, 3
    branch %unsafe_match3, ^unsafe_slow3, ^unsafe_check4

  block ^unsafe_check4:
    %unsafe_match4 = cmp eq i64 %value, 4
    branch %unsafe_match4, ^unsafe_slow0, ^unsafe_check5

  block ^unsafe_check5:
    %unsafe_match5 = cmp eq i64 %value, 5
    branch %unsafe_match5, ^unsafe_slow1, ^unsafe_check6

  block ^unsafe_check6:
    %unsafe_match6 = cmp eq i64 %value, 6
    branch %unsafe_match6, ^unsafe_slow2, ^unsafe_check7

  block ^unsafe_check7:
    %unsafe_match7 = cmp eq i64 %value, 7
    branch %unsafe_match7, ^unsafe_slow3, ^unsafe_check8

  block ^unsafe_check8:
    %unsafe_match8 = cmp eq i64 %value, 8
    branch %unsafe_match8, ^unsafe_slow0, ^unsafe_check9

  block ^unsafe_check9:
    %unsafe_match9 = cmp eq i64 %value, 9
    branch %unsafe_match9, ^unsafe_slow1, ^unsafe_check10

  block ^unsafe_check10:
    %unsafe_match10 = cmp eq i64 %value, 10
    branch %unsafe_match10, ^unsafe_slow2, ^unsafe_check11

  block ^unsafe_check11:
    %unsafe_match11 = cmp eq i64 %value, 11
    branch %unsafe_match11, ^unsafe_slow3, ^unsafe_check12

  block ^unsafe_check12:
    %unsafe_match12 = cmp eq i64 %value, 12
    branch %unsafe_match12, ^unsafe_slow0, ^unsafe_check13

  block ^unsafe_check13:
    %unsafe_match13 = cmp eq i64 %value, 13
    branch %unsafe_match13, ^unsafe_slow1, ^unsafe_check14

  block ^unsafe_check14:
    %unsafe_match14 = cmp eq i64 %value, 14
    branch %unsafe_match14, ^unsafe_slow2, ^unsafe_check15

  block ^unsafe_check15:
    %unsafe_match15 = cmp eq i64 %value, 15
    branch %unsafe_match15, ^unsafe_slow3, ^unsafe_check16

  block ^unsafe_check16:
    %unsafe_match16 = cmp eq i64 %value, 16
    branch %unsafe_match16, ^unsafe_slow0, ^unsafe_check17

  block ^unsafe_check17:
    %unsafe_match17 = cmp eq i64 %value, 17
    branch %unsafe_match17, ^unsafe_slow1, ^unsafe_check18

  block ^unsafe_check18:
    %unsafe_match18 = cmp eq i64 %value, 18
    branch %unsafe_match18, ^unsafe_slow2, ^unsafe_check19

  block ^unsafe_check19:
    %unsafe_match19 = cmp eq i64 %value, 19
    branch %unsafe_match19, ^unsafe_slow3, ^unsafe_check20

  block ^unsafe_check20:
    %unsafe_match20 = cmp eq i64 %value, 20
    branch %unsafe_match20, ^unsafe_slow0, ^unsafe_check21

  block ^unsafe_check21:
    %unsafe_match21 = cmp eq i64 %value, 21
    branch %unsafe_match21, ^unsafe_slow1, ^unsafe_check22

  block ^unsafe_check22:
    %unsafe_match22 = cmp eq i64 %value, 22
    branch %unsafe_match22, ^unsafe_slow2, ^unsafe_check23

  block ^unsafe_check23:
    %unsafe_match23 = cmp eq i64 %value, 23
    branch %unsafe_match23, ^unsafe_slow3, ^unsafe_check24

  block ^unsafe_check24:
    %unsafe_match24 = cmp eq i64 %value, 24
    branch %unsafe_match24, ^unsafe_slow0, ^unsafe_check25

  block ^unsafe_check25:
    %unsafe_match25 = cmp eq i64 %value, 25
    branch %unsafe_match25, ^unsafe_slow1, ^unsafe_check26

  block ^unsafe_check26:
    %unsafe_match26 = cmp eq i64 %value, 26
    branch %unsafe_match26, ^unsafe_slow2, ^unsafe_check27

  block ^unsafe_check27:
    %unsafe_match27 = cmp eq i64 %value, 27
    branch %unsafe_match27, ^unsafe_slow3, ^unsafe_check28

  block ^unsafe_check28:
    %unsafe_match28 = cmp eq i64 %value, 28
    branch %unsafe_match28, ^unsafe_slow0, ^unsafe_check29

  block ^unsafe_check29:
    %unsafe_match29 = cmp eq i64 %value, 29
    branch %unsafe_match29, ^unsafe_slow1, ^unsafe_check30

  block ^unsafe_check30:
    %unsafe_match30 = cmp eq i64 %value, 30
    branch %unsafe_match30, ^unsafe_slow2, ^unsafe_check31

  block ^unsafe_check31:
    %unsafe_match31 = cmp eq i64 %value, 31
    branch %unsafe_match31, ^unsafe_slow3, ^unsafe_check32

  block ^unsafe_check32:
    %unsafe_match32 = cmp eq i64 %value, 32
    branch %unsafe_match32, ^unsafe_slow0, ^unsafe_check33

  block ^unsafe_check33:
    %unsafe_match33 = cmp eq i64 %value, 33
    branch %unsafe_match33, ^unsafe_slow1, ^unsafe_check34

  block ^unsafe_check34:
    %unsafe_match34 = cmp eq i64 %value, 34
    branch %unsafe_match34, ^unsafe_slow2, ^unsafe_check35

  block ^unsafe_check35:
    %unsafe_match35 = cmp eq i64 %value, 35
    branch %unsafe_match35, ^unsafe_slow3, ^unsafe_check36

  block ^unsafe_check36:
    %unsafe_match36 = cmp eq i64 %value, 36
    branch %unsafe_match36, ^unsafe_slow0, ^unsafe_check37

  block ^unsafe_check37:
    %unsafe_match37 = cmp eq i64 %value, 37
    branch %unsafe_match37, ^unsafe_slow1, ^unsafe_check38

  block ^unsafe_check38:
    %unsafe_match38 = cmp eq i64 %value, 38
    branch %unsafe_match38, ^unsafe_slow2, ^unsafe_check39

  block ^unsafe_check39:
    %unsafe_match39 = cmp eq i64 %value, 39
    branch %unsafe_match39, ^unsafe_slow3, ^unsafe_check40

  block ^unsafe_check40:
    %unsafe_match40 = cmp eq i64 %value, 40
    branch %unsafe_match40, ^unsafe_slow0, ^unsafe_check41

  block ^unsafe_check41:
    %unsafe_match41 = cmp eq i64 %value, 41
    branch %unsafe_match41, ^unsafe_slow1, ^unsafe_check42

  block ^unsafe_check42:
    %unsafe_match42 = cmp eq i64 %value, 42
    branch %unsafe_match42, ^unsafe_slow2, ^unsafe_check43

  block ^unsafe_check43:
    %unsafe_match43 = cmp eq i64 %value, 43
    branch %unsafe_match43, ^unsafe_slow3, ^unsafe_check44

  block ^unsafe_check44:
    %unsafe_match44 = cmp eq i64 %value, 44
    branch %unsafe_match44, ^unsafe_slow0, ^unsafe_check45

  block ^unsafe_check45:
    %unsafe_match45 = cmp eq i64 %value, 45
    branch %unsafe_match45, ^unsafe_slow1, ^unsafe_check46

  block ^unsafe_check46:
    %unsafe_match46 = cmp eq i64 %value, 46
    branch %unsafe_match46, ^unsafe_slow2, ^unsafe_check47

  block ^unsafe_check47:
    %unsafe_match47 = cmp eq i64 %value, 47
    branch %unsafe_match47, ^unsafe_slow3, ^unsafe_check48

  block ^unsafe_check48:
    %unsafe_match48 = cmp eq i64 %value, 48
    branch %unsafe_match48, ^unsafe_slow0, ^unsafe_check49

  block ^unsafe_check49:
    %unsafe_match49 = cmp eq i64 %value, 49
    branch %unsafe_match49, ^unsafe_slow1, ^unsafe_check50

  block ^unsafe_check50:
    %unsafe_match50 = cmp eq i64 %value, 50
    branch %unsafe_match50, ^unsafe_slow2, ^unsafe_check51

  block ^unsafe_check51:
    %unsafe_match51 = cmp eq i64 %value, 51
    branch %unsafe_match51, ^unsafe_slow3, ^unsafe_check52

  block ^unsafe_check52:
    %unsafe_match52 = cmp eq i64 %value, 52
    branch %unsafe_match52, ^unsafe_slow0, ^unsafe_check53

  block ^unsafe_check53:
    %unsafe_match53 = cmp eq i64 %value, 53
    branch %unsafe_match53, ^unsafe_slow1, ^unsafe_check54

  block ^unsafe_check54:
    %unsafe_match54 = cmp eq i64 %value, 54
    branch %unsafe_match54, ^unsafe_slow2, ^unsafe_check55

  block ^unsafe_check55:
    %unsafe_match55 = cmp eq i64 %value, 55
    branch %unsafe_match55, ^unsafe_slow3, ^unsafe_check56

  block ^unsafe_check56:
    %unsafe_match56 = cmp eq i64 %value, 56
    branch %unsafe_match56, ^unsafe_slow0, ^unsafe_check57

  block ^unsafe_check57:
    %unsafe_match57 = cmp eq i64 %value, 57
    branch %unsafe_match57, ^unsafe_slow1, ^unsafe_check58

  block ^unsafe_check58:
    %unsafe_match58 = cmp eq i64 %value, 58
    branch %unsafe_match58, ^unsafe_slow2, ^unsafe_check59

  block ^unsafe_check59:
    %unsafe_match59 = cmp eq i64 %value, 59
    branch %unsafe_match59, ^unsafe_slow3, ^unsafe_fast

  block ^unsafe_fast:
    return void

  block ^unsafe_slow0:
    %unsafe_value0 = binary add i64 %value, 1
    call void @observe(%unsafe_value0)
    return void

  block ^unsafe_slow1:
    %unsafe_value1 = binary add i64 %value, 1
    call void @observe(%unsafe_value1)
    return void

  block ^unsafe_slow2:
    %unsafe_value2 = binary add i64 %value, 1
    call void @observe(%unsafe_value2)
    return void

  block ^unsafe_slow3:
    %unsafe_value3 = binary add i64 %value, 1
    call void @observe(%unsafe_value3)
    return void
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    store i64 0, @seen
    call void @versionable(100, 1)
    %fast_seen = load volatile i64 @seen
    %fast_bad = cmp ne i64 %fast_seen, 0
    call void @versionable(100, 0)
    call void @versionable(1, 1)
    %slow_seen = load volatile i64 @seen
    %slow_bad = cmp ne i64 %slow_seen, 1
    store i64 0, @seen
    %written_address = addr @written
    call void @effect_before_bailout(100, %written_address)
    %written_fast = load i64 @written
    %unsafe_fast_seen = load volatile i64 @seen
    %written_fast_bad = cmp ne i64 %written_fast, 100
    %unsafe_fast_seen_bad = cmp ne i64 %unsafe_fast_seen, 0
    call void @effect_before_bailout(2, %written_address)
    %written_slow = load i64 @written
    %unsafe_slow_seen = load volatile i64 @seen
    %written_slow_bad = cmp ne i64 %written_slow, 2
    %unsafe_slow_seen_bad = cmp ne i64 %unsafe_slow_seen, 3
    %bad0 = binary or i32 %fast_bad, %slow_bad
    %bad1 = binary or i32 %written_fast_bad, %unsafe_fast_seen_bad
    %bad2 = binary or i32 %written_slow_bad, %unsafe_slow_seen_bad
    %bad3 = binary or i32 %bad0, %bad1
    %bad = binary or i32 %bad3, %bad2
    return i32 %bad
}
