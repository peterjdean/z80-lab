; Clock benchmark. Runs a 65,535-iteration counting loop and HALTs.
; On an ideal 4 MHz Z80 this is roughly 0.426 seconds.
        ORG 0100H
        LD BC,0FFFFH
COUNT:  DEC BC
        LD A,B
        OR C
        JR NZ,COUNT
        HALT
