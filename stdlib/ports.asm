; Device port constants for the frozen v2 machine (see include/vm.h).
; This file is idempotent; include it directly when you only need port names.

IFNDEF STD_PORTS
EQU STD_PORTS 1

; Device 0: System
EQU SYS_VERSION 0x00
EQU SYS_DEBUG   0x01
EQU SYS_HALT    0x02

; Device 1: Console
EQU CONSOLE_PUTC      0x10
EQU CONSOLE_GETC      0x11
EQU CONSOLE_PUTI      0x12
EQU CONSOLE_PUTF      0x13
EQU CONSOLE_PUTS_ADDR 0x14
EQU CONSOLE_PUTS_LEN  0x15
EQU CONSOLE_PUTS      0x16
EQU CONSOLE_GETS_ADDR 0x17
EQU CONSOLE_GETS_MAX  0x18
EQU CONSOLE_GETS      0x19
EQU CONSOLE_GETS_LEN  0x1A
EQU CONSOLE_GETI      0x1B

ENDIF
