; Z80/MON v0.3 - generic 64K RAM machine monitor (Pasmo syntax)
;
; UART protocol: OUT (00H),A = TX; IN A,(01H) = RX;
;                IN A,(02H), bit 0 = data waiting.
;
; Syntax: D aaaa             Dump sixteen bytes plus ASCII
;         E aaaa xx [xx...]  Deposit consecutive bytes
;         G aaaa             Execute RAM routine (RET to monitor)
;         ?                  Help
;         Q                  HALT CPU (use debugger reset to restart)
;
; Origin 0100H. Monitor occupies RAM around 0100H; program RAM starts at 2000H.
; This is RAM, not protected ROM: don't edit monitor's own code/data!

        ORG 0100H
BOOT:   LD SP,0FFFEH
        LD HL,BANNER
        CALL PUTS

MAIN:   LD HL,PROMPT
        CALL PUTS
        CALL READLINE
        LD HL,BUFFER
        CALL SPACES
        LD A,(HL)
        OR A
        JP Z,MAIN
        CP '?'
        JP Z,HELP
        AND 0DFH              ; ASCII uppercase alphabetic commands
        INC HL
        CP 'D'
        JP Z,DUMP
        CP 'E'
        JP Z,ENTER
        CP 'G'
        JP Z,GO
        CP 'Q'
        JP Z,QUIT
        JP ERROR

HELP:   LD HL,HELPMSG
        CALL PUTS
        JP MAIN

; D aaaa: display address, sixteen hexadecimal bytes and ASCII.
DUMP:   CALL HEX16
        JP C,ERROR
        CALL ENDARGS
        JP NZ,ERROR
        PUSH DE
        LD HL,DUMPMSG
        CALL PUTS
        POP DE
        PUSH DE
        CALL PUTWORD
        LD A,':'
        CALL PUTC
        LD B,16
D_HEX:  LD A,' '
        CALL PUTC
        LD A,(DE)
        CALL PUTBYTE
        INC DE
        DJNZ D_HEX
        LD A,' '
        CALL PUTC
        LD A,'|'
        CALL PUTC
        POP DE
        LD B,16
D_ASC:  LD A,(DE)
        CP 20H
        JR C,D_DOT
        CP 7FH
        JR NC,D_DOT
        JR D_CHAR
D_DOT:  LD A,'.'
D_CHAR: CALL PUTC
        INC DE
        DJNZ D_ASC
        LD A,'|'
        CALL PUTC
        CALL NEWLINE
        JP MAIN

; E aaaa xx [xx...]: write one or more hex bytes, ascending addresses.
ENTER:  CALL HEX16
        JP C,ERROR
        LD (DEST),DE
        CALL SPACES
        LD A,(HL)
        OR A
        JP Z,ERROR
E_NEXT: LD B,2
        CALL HEXNUM
        JP C,ERROR
        LD A,E
        LD DE,(DEST)
        LD (DE),A
        INC DE
        LD (DEST),DE
        CALL SPACES
        LD A,(HL)
        OR A
        JR NZ,E_NEXT
        LD HL,OKMSG
        CALL PUTS
        JP MAIN

; G aaaa: call a RAM routine. A RET in user code returns to monitor.
; Executing HALT will halt the emulator; use debugger X to reset CPU.
GO:     CALL HEX16
        JP C,ERROR
        CALL ENDARGS
        JP NZ,ERROR
        EX DE,HL
        LD DE,GO_RETURN
        PUSH DE
        JP (HL)
GO_RETURN:
        LD HL,RETURNMSG
        CALL PUTS
        JP MAIN

QUIT:   CALL ENDARGS
        JP NZ,ERROR
        LD HL,BYEMSG
        CALL PUTS
        HALT

ERROR:  LD HL,ERRMSG
        CALL PUTS
        JP MAIN

; --- Serial console ---
PUTC:   OUT (00H),A
        RET
PUTS:   LD A,(HL)
        OR A
        RET Z
        CALL PUTC
        INC HL
        JR PUTS
NEWLINE:
        LD A,0DH
        CALL PUTC
        LD A,0AH
        JP PUTC
GETC:   IN A,(02H)
        AND 01H
        JR Z,GETC
        IN A,(01H)
        RET

; READLINE echoes printable input into BUFFER; final NUL added.
; Limit 63 input characters; CR terminates, LF is ignored.
READLINE:
        LD HL,BUFFER
        LD B,63
R_LOOP: CALL GETC
        CP 0AH
        JR Z,R_LOOP
        CP 0DH
        JR Z,R_DONE
        CP 08H
        JR Z,R_BACK
        CP 7FH
        JR Z,R_BACK
        CP 20H
        JR C,R_LOOP
        LD C,A
        LD A,B
        OR A
        JR Z,R_LOOP
        LD A,C
        LD (HL),A
        INC HL
        DEC B
        CALL PUTC
        JR R_LOOP
R_BACK: LD A,B
        CP 63
        JR Z,R_LOOP
        DEC HL
        INC B
        LD A,08H
        CALL PUTC
        LD A,' '
        CALL PUTC
        LD A,08H
        CALL PUTC
        JR R_LOOP
R_DONE: LD (HL),0
        CALL NEWLINE
        RET

; --- Hexadecimal input ---
SPACES: LD A,(HL)
        CP ' '
        JR Z,S_SKIP
        CP 09H
        RET NZ
S_SKIP: INC HL
        JR SPACES
ENDARGS:
        CALL SPACES
        LD A,(HL)
        OR A
        RET
HEX16:  LD B,4
HEXNUM: CALL SPACES
        LD DE,0
H_LOOP: LD A,(HL)
        CALL DIGIT
        RET C
        PUSH AF
        SLA E
        RL D
        SLA E
        RL D
        SLA E
        RL D
        SLA E
        RL D
        POP AF
        OR E
        LD E,A
        INC HL
        DJNZ H_LOOP
        OR A                    ; clear carry
        RET
DIGIT:  CP '0'
        JR C,H_BAD
        CP ':'
        JR C,H_NUM
        AND 0DFH
        CP 'A'
        JR C,H_BAD
        CP 'G'
        JR NC,H_BAD
        SUB 'A'-10
        RET
H_NUM:  SUB '0'
        RET
H_BAD:  SCF
        RET

; --- Hexadecimal output ---
PUTWORD:
        LD A,D
        CALL PUTBYTE
        LD A,E
PUTBYTE:
        PUSH AF
        RRCA
        RRCA
        RRCA
        RRCA
        AND 0FH
        CALL PUTNIB
        POP AF
        AND 0FH
PUTNIB: CP 0AH
        JR C,PN_DIG
        ADD A,7
PN_DIG: ADD A,'0'
        JP PUTC

BANNER: DB 0DH,0AH,'Z80/MON v0.3  (type ? for help)',0DH,0AH,0
PROMPT: DB 'MON> ',0
HELPMSG: DB 'D aaaa       dump 16 bytes',0DH,0AH
         DB 'E aaaa xx... enter consecutive hex bytes',0DH,0AH
         DB 'G aaaa       execute at address (RET to monitor)',0DH,0AH
         DB '?            help',0DH,0AH
         DB 'Q            halt CPU',0DH,0AH,0
DUMPMSG: DB 0
OKMSG: DB 'OK',0DH,0AH,0
RETURNMSG: DB 'Returned to monitor',0DH,0AH,0
ERRMSG: DB '? Syntax or hex error',0DH,0AH,0
BYEMSG: DB 'Monitor halted',0DH,0AH,0
DEST:   DW 0
BUFFER: DEFS 64,0
