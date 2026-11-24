; Multi-hop array transform + migrate + sum demo

        ; Reserve heap slots:
        ;   addr 0 : elem 0 (10)
        ;   addr 4 : elem 1 (20)
        ;   addr 8 : elem 2 (30)
        ;   addr 12: sum    (0)
        ;   addr 16: loop index i (0, 1, 2)
        ;   addr 20: current byte offset (0, 4, 8)

        PUSH 10
        PUSH 4
        ALLOC
        STORE32         ; addr 0 = 10

        PUSH 20
        PUSH 4
        ALLOC
        STORE32         ; addr 4 = 20

        PUSH 30
        PUSH 4
        ALLOC
        STORE32         ; addr 8 = 30

        PUSH 0
        PUSH 4
        ALLOC
        STORE32         ; addr 12 = 0 (sum)

        PUSH 0
        PUSH 4
        ALLOC
        STORE32         ; addr 16 = 0 (i)

        PUSH 0
        PUSH 4
        ALLOC
        STORE32         ; addr 20 = 0 (byte offset)

transform_loop:
        PUSH 16
        LOAD32          ; i
        PUSH 3
        CMP_LT
        JMP_Z migrate_stage

        ; new_val = heap[offset] + 5
        PUSH 20
        LOAD32          ; offset
        LOAD32          ; heap[offset]
        PUSH 5
        ADD             ; new_val

        ; store new_val back at heap[offset]
        DUP             ; new_val, new_val
        PUSH 20
        LOAD32          ; new_val, new_val, offset
        STORE32         ; heap[offset] = new_val; stack: [new_val]
        PRINT           ; prints 15, then 25, then 35

        ; offset += 4
        PUSH 20
        LOAD32
        PUSH 4
        ADD
        PUSH 20
        STORE32

        ; i += 1
        PUSH 16
        LOAD32
        PUSH 1
        ADD
        PUSH 16
        STORE32

        JMP transform_loop

migrate_stage:
        MIGRATE         ; move state + heap [15, 25, 35] to next node

        ; Sum heap[0] + heap[4] + heap[8] into heap[12]
        PUSH 0
        LOAD32
        PUSH 4
        LOAD32
        ADD
        PUSH 8
        LOAD32
        ADD             ; 15 + 25 + 35 = 75

        DUP
        PUSH 12
        STORE32         ; heap[12] = 75
        PRINT           ; prints 75
        HALT
