; Demonstrates constants/expressions, data directives, procedures with locals,
; floats, and host I/O.
;
; Static data lives in the image region at VM_IMAGE_START + 3 onward. Data
; labels resolve to absolute byte addresses, so LOAD64/PRINT_STR can read it
; directly. Heap addresses returned by ALLOC start at 0x4000.

EQU MSG_LEN  6
EQU VALUE    10
EQU SLOT     8

data:
STR "total="         ; data+0..data+5
DB 0                 ; data+6: NUL terminator
ALIGN 8              ; pad byte at data+7
value: DQ VALUE      ; data+8..data+15: big-endian 64-bit 10

; Print the string from the data section, followed by a newline.
PUSH data
PUSH MSG_LEN
PRINT_STR
PUSH 10
PRINT_CHAR

; Load the value from the data section and print it.
PUSH value
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
PUSH value
LOAD64
SET 0                ; local 0 = value
GET 0
GET 0
MUL
RET
