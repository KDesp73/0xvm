; Demonstrates constants/expressions, data directives, procedures with locals,
; floats, and host I/O.
;
; The data section is loaded into the heap at address 0 by an automatic
; prologue, so the string lives at 0..5 and the 64-bit value at 8..15.

EQU MSG_LEN  6
EQU VALUE    10
EQU SLOT     8

STR "total="         ; bytes 0..5
DB 0                 ; byte 6: NUL terminator
ALIGN 8              ; pad byte 7
DQ VALUE             ; bytes 8..15: big-endian 64-bit 10

; Print the string from the data section, followed by a newline.
PUSH 0
PUSH MSG_LEN
PRINT_STR
PUSH 10
PRINT_CHAR

; Load the value from the data section and print it.
PUSH SLOT
LOAD64
PRINT

; Square it in a procedure that reserves a local.
CALL square
PRINT

; (1.5 + 2.0) * 2.0 = 7.0
PUSHF 1.5
PUSHF 2.0
FADD
PUSHF 2.0
FMUL
DUP
FPRINT               ; leaves the raw bits on the stack

; A last integer calculation on the value 41.
PUSH 41
PUSH 1
ADD
PRINT                ; 42
HALT

; square: uses one local to hold its operand, then returns its square.
square:
ENTER 1
PUSH SLOT
LOAD64
SET 0                ; local 0 = heap[SLOT]
GET 0
GET 0
MUL
RET
