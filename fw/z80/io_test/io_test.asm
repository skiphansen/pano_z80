;       Console I/O test
;       Copyright (C) 2019 by Skip Hansen
;
        ORG     0               ;mem base of boot
;
;       I/O ports
;
CONDAT  EQU     1               ;console data port
DRIVE   EQU     10              ;fdc-port: # of drive
TRACK   EQU     11              ;fdc-port: # of track
SECTOR  EQU     12              ;fdc-port: # of sector
FDCOP   EQU     13              ;fdc-port: command
FDCST   EQU     14              ;fdc-port: status
DMAL    EQU     15              ;dma-port: dma address low
DMAH    EQU     16              ;dma-port: dma address high
;
        JP      COLD
;
TSTMSG: DEFM    'Hello Pano world!'
        DEFB    13,10,0

COLD:   LD      HL,TSTMSG
PRTMSG: LD      A,(HL)
        OR      A
        JP      Z,STOP
        OUT     (CONDAT),A
        INC     HL
        JP      PRTMSG
STOP:   DI
        HALT                    ;and halt cpu
;
        END

