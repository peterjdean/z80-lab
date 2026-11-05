; Polled UART demo: echo a typed line, then halt on carriage return.
; I/O 00 = transmit; I/O 01 = receive; I/O 02 = status.
; Status bit 0 = RX ready; status bit 1 = TX ready.
        ORG 0100H
START:  LD A,'>'
        OUT (00H),A
POLL:   IN A,(02H)
        AND 01H
        JR Z,POLL
        IN A,(01H)
        CP 0DH
        JR Z,DONE
        OUT (00H),A
        JR POLL
DONE:   LD A,0DH
        OUT (00H),A
        LD A,0AH
        OUT (00H),A
        HALT
