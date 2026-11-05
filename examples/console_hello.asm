; A null-terminated string printed via virtual serial port 00.
        ORG 0100H
START:  LD HL,MESSAGE
LOOP:   LD A,(HL)
        OR A
        JR Z,DONE
        OUT (00H),A
        INC HL
        JR LOOP
DONE:   HALT
MESSAGE: DB "Hello from Z80!",13,10,0
