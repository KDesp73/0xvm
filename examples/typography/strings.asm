; Static strings used by the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ data

intro:
    STR "TYPE | arrows move | type inserts | enter newline | bksp/del remove | esc quits"
    DB 0

title_text:
    STR "0XVM TYPE / BITMAP TYPOGRAPHY"
    DB 0

status_text:
    STR "PRINTABLE ASCII ENTERS TEXT | SPACE IS A CHARACTER"
    DB 0

done_text:
    STR "TYPOGRAPHY OK"
    DB 0

sample_text:
    STR "TYPE IS A LITTLE GRAPHICAL\nEDITOR. MOVE THE CARET WITH\nARROWS, TYPE TO INSERT TEXT,\nAND WATCH THE BITMAP FONT.\n"
    DB 0

