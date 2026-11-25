; Short aliases for the local-variable opcodes. This file is idempotent.

IFNDEF STD_LOCALS
EQU STD_LOCALS 1

MACRO GET i
    LOCAL_GET %i
ENDMACRO

MACRO SET i
    LOCAL_SET %i
ENDMACRO

ENDIF
