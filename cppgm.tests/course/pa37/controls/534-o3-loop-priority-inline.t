function @fail() -> void
    [binding=internal, return=noreturn, no_inline=yes] {
  block ^entry:
    unreachable
}

function @advance(%value : i64) -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %next = binary add i64 %value, 1
    return i64 %next
}

function @preferred_body(%value : i64) -> i64
    [binding=internal, unwind=no, inline_hint=yes] {
  block ^entry:
    %bad = cmp eq i64 %value, -1
    branch %bad, ^failure, ^work

  block ^failure:
    call void @fail()
    return i64 0

  block ^work:
    %v0 = call i64 @advance(%value)
    %v1 = call i64 @advance(%v0)
    %v2 = call i64 @advance(%v1)
    %v3 = call i64 @advance(%v2)
    %v4 = call i64 @advance(%v3)
    %v5 = call i64 @advance(%v4)
    %v6 = call i64 @advance(%v5)
    %v7 = call i64 @advance(%v6)
    %v8 = call i64 @advance(%v7)
    %v9 = call i64 @advance(%v8)
    %v10 = call i64 @advance(%v9)
    %v11 = call i64 @advance(%v10)
    %v12 = call i64 @advance(%v11)
    %v13 = call i64 @advance(%v12)
    %v14 = call i64 @advance(%v13)
    %v15 = call i64 @advance(%v14)
    %v16 = call i64 @advance(%v15)
    %v17 = call i64 @advance(%v16)
    %v18 = call i64 @advance(%v17)
    %v19 = call i64 @advance(%v18)
    %v20 = call i64 @advance(%v19)
    %v21 = call i64 @advance(%v20)
    %v22 = call i64 @advance(%v21)
    %v23 = call i64 @advance(%v22)
    %v24 = call i64 @advance(%v23)
    %v25 = call i64 @advance(%v24)
    %v26 = call i64 @advance(%v25)
    %v27 = call i64 @advance(%v26)
    %v28 = call i64 @advance(%v27)
    %v29 = call i64 @advance(%v28)
    %v30 = call i64 @advance(%v29)
    %v31 = call i64 @advance(%v30)
    %v32 = call i64 @advance(%v31)
    %v33 = call i64 @advance(%v32)
    %v34 = call i64 @advance(%v33)
    %v35 = call i64 @advance(%v34)
    %v36 = call i64 @advance(%v35)
    %v37 = call i64 @advance(%v36)
    %v38 = call i64 @advance(%v37)
    %v39 = call i64 @advance(%v38)
    %v40 = call i64 @advance(%v39)
    %v41 = call i64 @advance(%v40)
    %v42 = call i64 @advance(%v41)
    %v43 = call i64 @advance(%v42)
    %v44 = call i64 @advance(%v43)
    %v45 = call i64 @advance(%v44)
    %v46 = call i64 @advance(%v45)
    %v47 = call i64 @advance(%v46)
    %v48 = call i64 @advance(%v47)
    %v49 = call i64 @advance(%v48)
    %v50 = call i64 @advance(%v49)
    %v51 = call i64 @advance(%v50)
    %v52 = call i64 @advance(%v51)
    %v53 = call i64 @advance(%v52)
    %v54 = call i64 @advance(%v53)
    %v55 = call i64 @advance(%v54)
    %v56 = call i64 @advance(%v55)
    %v57 = call i64 @advance(%v56)
    %v58 = call i64 @advance(%v57)
    %v59 = call i64 @advance(%v58)
    %v60 = call i64 @advance(%v59)
    %v61 = call i64 @advance(%v60)
    %v62 = call i64 @advance(%v61)
    %v63 = call i64 @advance(%v62)
    %v64 = call i64 @advance(%v63)
    %v65 = call i64 @advance(%v64)
    %v66 = call i64 @advance(%v65)
    %v67 = call i64 @advance(%v66)
    %v68 = call i64 @advance(%v67)
    %v69 = call i64 @advance(%v68)
    %v70 = call i64 @advance(%v69)
    %v71 = call i64 @advance(%v70)
    %v72 = call i64 @advance(%v71)
    %v73 = call i64 @advance(%v72)
    %v74 = call i64 @advance(%v73)
    %v75 = call i64 @advance(%v74)
    %v76 = call i64 @advance(%v75)
    %v77 = call i64 @advance(%v76)
    %v78 = call i64 @advance(%v77)
    %v79 = call i64 @advance(%v78)
    %v80 = call i64 @advance(%v79)
    %v81 = call i64 @advance(%v80)
    %v82 = call i64 @advance(%v81)
    %v83 = call i64 @advance(%v82)
    %v84 = call i64 @advance(%v83)
    %v85 = call i64 @advance(%v84)
    %v86 = call i64 @advance(%v85)
    %v87 = call i64 @advance(%v86)
    %v88 = call i64 @advance(%v87)
    %v89 = call i64 @advance(%v88)
    %v90 = call i64 @advance(%v89)
    %v91 = call i64 @advance(%v90)
    %v92 = call i64 @advance(%v91)
    %v93 = call i64 @advance(%v92)
    %v94 = call i64 @advance(%v93)
    %v95 = call i64 @advance(%v94)
    %v96 = call i64 @advance(%v95)
    %v97 = call i64 @advance(%v96)
    %v98 = call i64 @advance(%v97)
    %v99 = call i64 @advance(%v98)
    %v100 = call i64 @advance(%v99)
    %v101 = call i64 @advance(%v100)
    %v102 = call i64 @advance(%v101)
    %v103 = call i64 @advance(%v102)
    %v104 = call i64 @advance(%v103)
    %v105 = call i64 @advance(%v104)
    %v106 = call i64 @advance(%v105)
    %v107 = call i64 @advance(%v106)
    %v108 = call i64 @advance(%v107)
    %v109 = call i64 @advance(%v108)
    %v110 = call i64 @advance(%v109)
    %v111 = call i64 @advance(%v110)
    %v112 = call i64 @advance(%v111)
    %v113 = call i64 @advance(%v112)
    %v114 = call i64 @advance(%v113)
    %v115 = call i64 @advance(%v114)
    %v116 = call i64 @advance(%v115)
    %v117 = call i64 @advance(%v116)
    %v118 = call i64 @advance(%v117)
    %v119 = call i64 @advance(%v118)
    %v120 = call i64 @advance(%v119)
    %v121 = call i64 @advance(%v120)
    %v122 = call i64 @advance(%v121)
    %v123 = call i64 @advance(%v122)
    %v124 = call i64 @advance(%v123)
    %v125 = call i64 @advance(%v124)
    %v126 = call i64 @advance(%v125)
    %v127 = call i64 @advance(%v126)
    %v128 = call i64 @advance(%v127)
    %v129 = call i64 @advance(%v128)
    %v130 = call i64 @advance(%v129)
    %v131 = call i64 @advance(%v130)
    %v132 = call i64 @advance(%v131)
    %v133 = call i64 @advance(%v132)
    %v134 = call i64 @advance(%v133)
    %v135 = call i64 @advance(%v134)
    %v136 = call i64 @advance(%v135)
    %v137 = call i64 @advance(%v136)
    %v138 = call i64 @advance(%v137)
    %v139 = call i64 @advance(%v138)
    %v140 = call i64 @advance(%v139)
    %v141 = call i64 @advance(%v140)
    %v142 = call i64 @advance(%v141)
    %v143 = call i64 @advance(%v142)
    %v144 = call i64 @advance(%v143)
    %v145 = call i64 @advance(%v144)
    %v146 = call i64 @advance(%v145)
    %v147 = call i64 @advance(%v146)
    %v148 = call i64 @advance(%v147)
    %v149 = call i64 @advance(%v148)
    %v150 = call i64 @advance(%v149)
    %v151 = call i64 @advance(%v150)
    %v152 = call i64 @advance(%v151)
    %v153 = call i64 @advance(%v152)
    %v154 = call i64 @advance(%v153)
    %v155 = call i64 @advance(%v154)
    %v156 = call i64 @advance(%v155)
    %v157 = call i64 @advance(%v156)
    %v158 = call i64 @advance(%v157)
    %v159 = call i64 @advance(%v158)
    %v160 = call i64 @advance(%v159)
    %v161 = call i64 @advance(%v160)
    %v162 = call i64 @advance(%v161)
    %v163 = call i64 @advance(%v162)
    %v164 = call i64 @advance(%v163)
    %v165 = call i64 @advance(%v164)
    %v166 = call i64 @advance(%v165)
    %v167 = call i64 @advance(%v166)
    %v168 = call i64 @advance(%v167)
    %v169 = call i64 @advance(%v168)
    %v170 = call i64 @advance(%v169)
    %v171 = call i64 @advance(%v170)
    %v172 = call i64 @advance(%v171)
    %v173 = call i64 @advance(%v172)
    %v174 = call i64 @advance(%v173)
    %v175 = call i64 @advance(%v174)
    %v176 = call i64 @advance(%v175)
    %v177 = call i64 @advance(%v176)
    %v178 = call i64 @advance(%v177)
    %v179 = call i64 @advance(%v178)
    %v180 = call i64 @advance(%v179)
    %v181 = call i64 @advance(%v180)
    %v182 = call i64 @advance(%v181)
    %v183 = call i64 @advance(%v182)
    %v184 = call i64 @advance(%v183)
    %v185 = call i64 @advance(%v184)
    %v186 = call i64 @advance(%v185)
    %v187 = call i64 @advance(%v186)
    %v188 = call i64 @advance(%v187)
    %v189 = call i64 @advance(%v188)
    %v190 = call i64 @advance(%v189)
    %v191 = call i64 @advance(%v190)
    %v192 = call i64 @advance(%v191)
    %v193 = call i64 @advance(%v192)
    %v194 = call i64 @advance(%v193)
    %v195 = call i64 @advance(%v194)
    %v196 = call i64 @advance(%v195)
    %v197 = call i64 @advance(%v196)
    %v198 = call i64 @advance(%v197)
    %v199 = call i64 @advance(%v198)
    %v200 = call i64 @advance(%v199)
    %v201 = call i64 @advance(%v200)
    %v202 = call i64 @advance(%v201)
    %v203 = call i64 @advance(%v202)
    %v204 = call i64 @advance(%v203)
    %v205 = call i64 @advance(%v204)
    %v206 = call i64 @advance(%v205)
    %v207 = call i64 @advance(%v206)
    %v208 = call i64 @advance(%v207)
    %v209 = call i64 @advance(%v208)
    %v210 = call i64 @advance(%v209)
    %v211 = call i64 @advance(%v210)
    %v212 = call i64 @advance(%v211)
    %v213 = call i64 @advance(%v212)
    %v214 = call i64 @advance(%v213)
    %v215 = call i64 @advance(%v214)
    %v216 = call i64 @advance(%v215)
    %v217 = call i64 @advance(%v216)
    %v218 = call i64 @advance(%v217)
    %v219 = call i64 @advance(%v218)
    %v220 = call i64 @advance(%v219)
    %v221 = call i64 @advance(%v220)
    %v222 = call i64 @advance(%v221)
    %v223 = call i64 @advance(%v222)
    %v224 = call i64 @advance(%v223)
    %v225 = call i64 @advance(%v224)
    %v226 = call i64 @advance(%v225)
    %v227 = call i64 @advance(%v226)
    %v228 = call i64 @advance(%v227)
    %v229 = call i64 @advance(%v228)
    %v230 = call i64 @advance(%v229)
    %v231 = call i64 @advance(%v230)
    %v232 = call i64 @advance(%v231)
    %v233 = call i64 @advance(%v232)
    %v234 = call i64 @advance(%v233)
    %v235 = call i64 @advance(%v234)
    %v236 = call i64 @advance(%v235)
    %v237 = call i64 @advance(%v236)
    %v238 = call i64 @advance(%v237)
    %v239 = call i64 @advance(%v238)
    %v240 = call i64 @advance(%v239)
    %v241 = call i64 @advance(%v240)
    %v242 = call i64 @advance(%v241)
    %v243 = call i64 @advance(%v242)
    %v244 = call i64 @advance(%v243)
    %v245 = call i64 @advance(%v244)
    %v246 = call i64 @advance(%v245)
    %v247 = call i64 @advance(%v246)
    %v248 = call i64 @advance(%v247)
    %v249 = call i64 @advance(%v248)
    %v250 = call i64 @advance(%v249)
    %v251 = call i64 @advance(%v250)
    %v252 = call i64 @advance(%v251)
    %v253 = call i64 @advance(%v252)
    %v254 = call i64 @advance(%v253)
    %v255 = call i64 @advance(%v254)
    %v256 = call i64 @advance(%v255)
    %v257 = call i64 @advance(%v256)
    %v258 = call i64 @advance(%v257)
    %v259 = call i64 @advance(%v258)
    return i64 %v259
}

function @loop_first(%limit : i64) -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %cold = call i64 @preferred_body(0)
    jump ^header

  block ^header:
    %index = phi i64 [^entry: 0, ^latch: %next_index]
    %sum = phi i64 [^entry: %cold, ^latch: %hot]
    %more = cmp lt i64 %index, %limit
    branch %more, ^body, ^exit

  block ^body:
    %hot = call i64 @preferred_body(%sum)
    jump ^latch

  block ^latch:
    %next_index = binary add i64 %index, 1
    jump ^header

  block ^exit:
    return i64 %sum
}

function @loop_second(%limit : i64) -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %cold = call i64 @preferred_body(0)
    jump ^header

  block ^header:
    %index = phi i64 [^entry: 0, ^latch: %next_index]
    %sum = phi i64 [^entry: %cold, ^latch: %hot]
    %more = cmp lt i64 %index, %limit
    branch %more, ^body, ^exit

  block ^body:
    %hot = call i64 @preferred_body(%sum)
    jump ^latch

  block ^latch:
    %next_index = binary add i64 %index, 1
    jump ^header

  block ^exit:
    return i64 %sum
}

function @no_loop_pair() -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    %first = call i64 @preferred_body(0)
    %second = call i64 @preferred_body(%first)
    return i64 %second
}

function @two_loop_calls() -> i64 [unwind=no, no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %index = phi i64 [^entry: 0, ^body: %next_index]
    %sum = phi i64 [^entry: 0, ^body: %second]
    %more = cmp lt i64 %index, 1
    branch %more, ^body, ^exit

  block ^body:
    %first = call i64 @preferred_body(%sum)
    %second = call i64 @preferred_body(%first)
    %next_index = binary add i64 %index, 1
    jump ^header

  block ^exit:
    return i64 %sum
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %first = call i64 @loop_first(5)
    %second = call i64 @loop_second(5)
    %plain = call i64 @no_loop_pair()
    %looped = call i64 @two_loop_calls()
    %sum0 = binary add i64 %first, %second
    %sum1 = binary add i64 %sum0, %plain
    %sum2 = binary add i64 %sum1, %looped
    %bad = cmp ne i64 %sum2, 4160
  return i64 %bad
}
