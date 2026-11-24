; =============================================================================
; Descriptive statistics tool
;
; Reads a count N, then N integers, then a report mode (0 = short, 1 = full).
; Computes min, max, sum, mean, population variance and standard deviation
; using a heap array, procedures with locals, and floating-point arithmetic.
; The report routine is selected through an indirect CALL_I dispatch table.
;
; Input (one value per line):
;     5
;     10
;     20
;     30
;     40
;     50
;     1
;
; =============================================================================

; -----------------------------------------------------------------------------
; Data section -- emitted into the heap at address 0 by an implicit prologue.
; PRINT_STR appends its own newline, so labels contain no embedded newline.
; -----------------------------------------------------------------------------
    STR         "n="
    STR         "min="
    STR         "max="
    STR         "sum="
    STR         "mean="
    STR         "var="
    STR         "sd="
    ALIGN       4                   ; 28 data bytes, so the heap starts at BASE

; -----------------------------------------------------------------------------
; String table: address and length of every label
; -----------------------------------------------------------------------------
    EQU         S_N         0
    EQU         S_MIN       2
    EQU         S_MAX       6
    EQU         S_SUM       10
    EQU         S_MEAN      14
    EQU         S_VAR       19
    EQU         S_SD        23

    EQU         SLEN_N      2
    EQU         SLEN_MIN    4
    EQU         SLEN_MAX    4
    EQU         SLEN_SUM    4
    EQU         SLEN_MEAN   5
    EQU         SLEN_VAR    4
    EQU         SLEN_SD     3

; -----------------------------------------------------------------------------
; Globals block: 64 bytes at BASE. Offsets below are relative to BASE.
; -----------------------------------------------------------------------------
    EQU         BASE        28

    EQU         O_N         0       ; i32   count, clamped to 1..1000
    EQU         O_ARR       4       ; i32   address of the array block
    EQU         O_MIN       8       ; i32   smallest input
    EQU         O_MAX       12      ; i32   largest input
    EQU         O_SUM       16      ; i64   sum of inputs
    EQU         O_MEAN      24      ; f64   mean
    EQU         O_VAR       32      ; f64   population variance
    EQU         O_SD        40      ; f64   standard deviation
    EQU         O_I         48      ; i32   shared loop counter
    EQU         O_SHORT     56      ; i32   address of print_short
    EQU         O_FULL      60      ; i32   address of print_full

; -----------------------------------------------------------------------------
; Local indices used by compute_stats (ENTER 7 reserves slots 0 through 6)
; -----------------------------------------------------------------------------
    EQU         LOC_I       0
    EQU         LOC_MIN     1
    EQU         LOC_MAX     2
    EQU         LOC_SUM     3
    EQU         LOC_MEAN    4
    EQU         LOC_SSQ     5       ; sum of squared deviations, then variance
    EQU         LOC_X       6

; =============================================================================
; main: read input, compute statistics, print a report, release the array
; =============================================================================

; Read N and clamp it into [1, 1000] so the allocation stays bounded.
    READ_INT
    DUP
    PUSH        1
    CMP_LT
    JMP_Z       n_low_ok
    POP
    PUSH        1
n_low_ok:
    DUP
    PUSH        1000
    CMP_GT
    JMP_Z       n_high_ok
    POP
    PUSH        1000
n_high_ok:

; Allocate the globals block (its address is BASE) and store N.
    PUSH        64
    ALLOC
    POP
    PUSH        BASE+O_N
    STORE32

; Allocate an array of N 32-bit integers and remember its address.
    PUSH        BASE+O_N
    LOAD32
    PUSH        4
    MUL
    ALLOC
    PUSH        BASE+O_ARR
    STORE32

; Read the N values into the array.
    PUSH        0
    PUSH        BASE+O_I
    STORE32

read_loop:
    PUSH        BASE+O_I
    LOAD32
    PUSH        BASE+O_N
    LOAD32
    CMP_LT                          ; i < N ?
    JMP_Z       read_done

    READ_INT
    PUSH        BASE+O_ARR
    LOAD32                          ; array base
    PUSH        BASE+O_I
    LOAD32                          ; i
    PUSH        4
    MUL                             ; i * 4
    ADD                             ; array base + i * 4
    STORE32                         ; array[i] = value

    PUSH        BASE+O_I
    LOAD32
    INC
    PUSH        BASE+O_I
    STORE32
    JMP         read_loop
read_done:

    CALL        compute_stats
    POP                             ; discard the procedure's dummy result

; Build the indirect-call table: two 4-byte code addresses.
    PUSH        print_short
    PUSH        BASE+O_SHORT
    STORE32
    PUSH        print_full
    PUSH        BASE+O_FULL
    STORE32

; mode & 1 selects one of the two report routines.
    READ_INT
    PUSH        1
    AND
    PUSH        4                   ; table entries are 4 bytes wide
    MUL
    PUSH        BASE+O_SHORT
    ADD
    LOAD32                          ; code address
    CALL_I
    POP

; Hand the array block back to the free list.
    PUSH        BASE+O_ARR
    LOAD32
    FREE
    HALT

; =============================================================================
; compute_stats: derive every statistic from the array into the globals
; =============================================================================
compute_stats:
    ENTER       7                   ; reserve LOC_I through LOC_X

    PUSH        0
    SET         LOC_I
    PUSH        BASE+O_ARR
    LOAD32
    LOAD32                          ; array[0]
    SET         LOC_MIN
    PUSH        BASE+O_ARR
    LOAD32
    LOAD32
    SET         LOC_MAX
    PUSH        0
    SET         LOC_SUM

; Pass 1 -- min, max and sum.
stats_l1:
    GET         LOC_I
    PUSH        BASE+O_N
    LOAD32
    CMP_LT                          ; i < N ?
    JMP_Z       stats_l2

    PUSH        BASE+O_ARR
    LOAD32
    GET         LOC_I
    PUSH        4
    MUL
    ADD
    LOAD32
    SET         LOC_X               ; x = array[i]

; OVER/SWAP keep x on the stack while the comparison runs. The branch that
; loses the comparison pops the leftover x to restore the stack baseline.
    GET         LOC_X
    GET         LOC_MIN
    OVER
    SWAP
    CMP_LT                          ; x < min ?
    JMP_Z       min_no
    SET         LOC_MIN
    JMP         min_end
min_no:
    POP
min_end:

    GET         LOC_X
    GET         LOC_MAX
    OVER
    SWAP
    CMP_GT                          ; x > max ?
    JMP_Z       max_no
    SET         LOC_MAX
    JMP         max_end
max_no:
    POP
max_end:

    GET         LOC_SUM
    GET         LOC_X
    ADD
    SET         LOC_SUM

    GET         LOC_I
    INC
    SET         LOC_I
    JMP         stats_l1

; Pass 2 -- mean, then the sum of squared deviations.
stats_l2:
    GET         LOC_SUM
    ITOF
    PUSH        BASE+O_N
    LOAD32
    ITOF
    FDIV
    SET         LOC_MEAN            ; mean = (double)sum / N

    PUSHF       0.0
    SET         LOC_SSQ

    PUSH        0
    SET         LOC_I

stats_l3:
    GET         LOC_I
    PUSH        BASE+O_N
    LOAD32
    CMP_LT                          ; i < N ?
    JMP_Z       stats_done

    PUSH        BASE+O_ARR
    LOAD32
    GET         LOC_I
    PUSH        4
    MUL
    ADD
    LOAD32
    ITOF                            ; (double)array[i]
    GET         LOC_MEAN
    FSUB                            ; deviation = x - mean
    PUSH        0                   ; PICK reads its index from the stack
    PICK                            ; duplicate the deviation
    FMUL                            ; square it
    GET         LOC_SSQ
    FADD
    SET         LOC_SSQ             ; sumsq += deviation * deviation

    GET         LOC_I
    INC
    SET         LOC_I
    JMP         stats_l3

stats_done:
    GET         LOC_SSQ
    PUSH        BASE+O_N
    LOAD32
    ITOF
    FDIV
    SET         LOC_SSQ             ; variance = sumsq / N

    GET         LOC_SSQ
    FSQRT
    PUSH        BASE+O_SD
    STORE64

    GET         LOC_SSQ
    PUSH        BASE+O_VAR
    STORE64

    GET         LOC_MEAN
    PUSH        BASE+O_MEAN
    STORE64

    GET         LOC_MIN
    PUSH        BASE+O_MIN
    STORE32

    GET         LOC_MAX
    PUSH        BASE+O_MAX
    STORE32

    GET         LOC_SUM
    PUSH        BASE+O_SUM
    STORE64

    PUSH        0
    RET

; =============================================================================
; print_short: mean and standard deviation only
; =============================================================================
print_short:
    ENTER       0

    PUSH        S_MEAN
    PUSH        SLEN_MEAN
    PRINT_STR
    PUSH        BASE+O_MEAN
    LOAD64
    FPRINT

    PUSH        S_SD
    PUSH        SLEN_SD
    PRINT_STR
    PUSH        BASE+O_SD
    LOAD64
    FPRINT

    PUSH        0
    RET

; =============================================================================
; print_full: all six statistics
; =============================================================================
print_full:
    ENTER       0

    PUSH        S_N
    PUSH        SLEN_N
    PRINT_STR
    PUSH        BASE+O_N
    LOAD32
    PRINT

    PUSH        S_MIN
    PUSH        SLEN_MIN
    PRINT_STR
    PUSH        BASE+O_MIN
    LOAD32
    PRINT

    PUSH        S_MAX
    PUSH        SLEN_MAX
    PRINT_STR
    PUSH        BASE+O_MAX
    LOAD32
    PRINT

    PUSH        S_SUM
    PUSH        SLEN_SUM
    PRINT_STR
    PUSH        BASE+O_SUM
    LOAD64
    PRINT

    PUSH        S_MEAN
    PUSH        SLEN_MEAN
    PRINT_STR
    PUSH        BASE+O_MEAN
    LOAD64
    FPRINT

    PUSH        S_VAR
    PUSH        SLEN_VAR
    PRINT_STR
    PUSH        BASE+O_VAR
    LOAD64
    FPRINT

    PUSH        S_SD
    PUSH        SLEN_SD
    PRINT_STR
    PUSH        BASE+O_SD
    LOAD64
    FPRINT

    PUSH        0
    RET
