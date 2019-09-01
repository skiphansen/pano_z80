; Z80 CBIOS Panologic devices by Skip Hansen 8/2019
;
; This implementation supports an SD card image for Grant Searle's 
; Multicomp project.
;
; This code is based on code from both of the above projects, copyright
; statements follow:

;==================================================================================
;       Z80 CBIOS for Z80-Simulator
;
;       Copyright (C) 1988-2007 by Udo Munk
;==================================================================================

; and

;==================================================================================
; Contents of this file are copyright Grant Searle
; Blocking/unblocking routines are the published version by Digital Research
; (bugfixed, as found on the web)
;
; You have permission to use this for NON COMMERCIAL USE ONLY
; If you wish to use it elsewhere, please include an acknowledgement to myself.
;
; http://searle.hostei.com/grant/index.html
;
; eMail: home.micros01@btinternet.com
;
; If the above don't work, please perform an Internet search to see if I have
; updated the web page hosting service.
;==================================================================================

;       
; Drive mappings:
;
; A: .. P: 8 MB hard disk images from Multicomp SD card image
; 
; Note: The system track from the SD card image is replaced by the 
; I/O processor on the fly with data from the boot.dsk image.

MSIZE   EQU     60              ;cp/m version memory size in kilobytes
;
;       "bias" is address offset from 3400H for memory systems
;       than 16K (referred to as "b" throughout the text).
;
BIAS    EQU     (MSIZE-20)*1024
CCP     EQU     3400H+BIAS      ;base of ccp
BDOS    EQU     CCP+806H        ;base of bdos
BIOS    EQU     CCP+1600H       ;base of bios
NSECTS  EQU     (BIOS-CCP)/128  ;warm start sector count
CDISK   EQU     0004H           ;current disk number 0=A,...,15=P
IOBYTE  EQU     0003H           ;intel i/o byte
;
;       I/O ports
;
CONSTA  EQU     0               ;console status port
CONDAT  EQU     1               ;console data port
PRTSTA  EQU     2               ;printer status port
PRTDAT  EQU     3               ;printer data port
AUXDAT  EQU     5               ;auxiliary data port
FDCD    EQU     10              ;fdc-port: # of drive
FDCT    EQU     11              ;fdc-port: # of track
FDCS    EQU     12              ;fdc-port: # of sector
FDCOP   EQU     13              ;fdc-port: command
FDCST   EQU     14              ;fdc-port: status
DMAL    EQU     15              ;dma-port: dma address low
DMAH    EQU     16              ;dma-port: dma address high
;
        ORG     BIOS            ;origin of this program
;
;       jump vector for individual subroutines
;
        JP      BOOT            ;cold start
WBOOTE: JP      WBOOT           ;warm start
        JP      CONST           ;console status
        JP      CONIN           ;console character in
        JP      CONOUT          ;console character out
        JP      LIST            ;list character out
        JP      PUNCH           ;punch character out
        JP      READER          ;reader character in
        JP      HOME            ;move head to home position
        JP      SELDSK          ;select disk
        JP      SETTRK          ;set track number
        JP      SETSEC          ;set sector number
        JP      SETDMA          ;set dma address
        JP      READ            ;read disk
        JP      WRITE           ;write disk
        JP      LISTST          ;return list status
        JP      SECTRAN         ;sector translate

; Disk parameter block for Multicomp boot harddisk (A:)
dpb0:
        DEFW 128        ;SPT - sectors per track
        DEFB 5          ;BSH - block shift factor
        DEFB 31         ;BLM - block mask
        DEFB 1          ;EXM - Extent mask
        DEFW 2043        ; (2047-4) DSM - Storage size (blocks - 1)
        DEFW 511        ;DRM - Number of directory entries - 1
        DEFB 240        ;AL0 - 1 bit set per directory block
        DEFB 0          ;AL1 -            "
        DEFW 0          ;CKS - DIR check vector size (DRM+1)/4 (0=fixed disk)
        DEFW 1          ;OFF - Reserved tracks

; Disk parameter block for Multicomp 8MB harddisk images (B:...P:)
dpb:
        DEFW 128        ;SPT - sectors per track
        DEFB 5          ;BSH - block shift factor
        DEFB 31         ;BLM - block mask
        DEFB 1          ;EXM - Extent mask
        DEFW 2047        ;DSM - Storage size (blocks - 1)
        DEFW 511        ;DRM - Number of directory entries - 1
        DEFB 240        ;AL0 - 1 bit set per directory block
        DEFB 0          ;AL1 -            "
        DEFW 0          ;CKS - DIR check vector size (DRM+1)/4 (0=fixed disk)
        DEFW 0          ;OFF - Reserved tracks

;Disk parameter header for Multicomp 8 MB hard disks A: to F:
DPBASE: DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb0,0000h,alv00  ;A:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv01   ;B:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv02   ;C:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv03   ;D:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv04   ;E:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv05   ;F:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv06   ;G:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv07   ;H:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv08   ;I:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv09   ;J:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv10   ;K:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv11   ;L:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv12   ;M:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv13   ;N:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv14   ;O:
        DEFW    0000h,0000h,0000h,0000h,DIRBF,dpb,0000h,alv15   ;P:

;
;       messages
;
SIGNON: DEFM    '64K CP/M Vers. 2.2'
        DEFB    13,10
        DEFM    'Z80 CBIOS V0.1 (Multicomp version) for PanoLogic by Skip Hansen'
        DEFB    13,10,0
;
LDERR:  DEFM    'BIOS: error booting'
        DEFB    13,10,0

;
;       end of fixed tables
;
;       utility functions
;
;       print a 0 terminated string to console device
;       pointer to string in HL
;
PRTMSG: LD      A,(HL)
        OR      A
        RET     Z
        LD      C,A
        CALL    CONOUT
        INC     HL
        JP      PRTMSG
;
;       individual subroutines to perform each function
;       simplest case is to just perform parameter initialization
;
BOOT:   LD      SP,80H          ;use space below buffer for stack
        LD      HL,SIGNON       ;print message
        CALL    PRTMSG
        XOR     A               ;zero in the accum
        LD      (IOBYTE),A      ;clear the iobyte
        LD      (CDISK),A       ;select disk zero
        JP      GOCPM           ;initialize and go to cp/m
;
;       simplest case is to read the disk until all sectors loaded
;
WBOOT:  LD      SP,80H          ;use space below buffer for stack
        LD      C,0             ;select disk 0
        CALL    SELDSK
        CALL    HOME            ;go to track 00
;
        LD      B,NSECTS        ;b counts # of sectors to load
        LD      C,0             ;c has the current track number
        LD      D,2             ;d has the next sector to read
;       note that we begin by reading track 0, sector 2 since sector 1
;       contains the cold start loader, which is skipped in a warm start
        LD      HL,CCP          ;base of cp/m (initial load point)
LOAD1:                          ;load one more sector
        PUSH    BC              ;save sector count, current track
        PUSH    DE              ;save next sector to read
        PUSH    HL              ;save dma address
        LD      C,D             ;get sector address to register c
        CALL    SETSEC          ;set sector address from register c
        POP     BC              ;recall dma address to b,c
        PUSH    BC              ;replace on stack for later recall
        CALL    SETDMA          ;set dma address from b,c
;       drive set to 0, track set, sector set, dma address set
        CALL    READ
        OR      A               ;any errors?
        JP      Z,LOAD2         ;no, continue
        LD      HL,LDERR        ;error, print message
        CALL    PRTMSG
        DI                      ;and halt the machine
        HALT
;       no error, move to next sector
LOAD2:  POP     HL              ;recall dma address
        LD      DE,128          ;dma=dma+128
        ADD     HL,DE           ;new dma address is in h,l
        POP     DE              ;recall sector address
        POP     BC              ;recall number of sectors remaining,
                                ;and current trk
        DEC     B               ;sectors=sectors-1
        JP      Z,GOCPM         ;transfer to cp/m if all have been loaded
;       more sectors remain to load, check for track change
        INC     D
        JP      LOAD1           ;for another sector
;       end of load operation, set parameters and go to cp/m
GOCPM:
        LD      A,0C3H          ;c3 is a jmp instruction
        LD      (0),A           ;for jmp to wboot
        LD      HL,WBOOTE       ;wboot entry point
        LD      (1),HL          ;set address field for jmp at 0
;
        LD      (5),A           ;for jmp to bdos
        LD      HL,BDOS         ;bdos entry point
        LD      (6),HL          ;address field of jump at 5 to bdos
;
        LD      BC,80H          ;default dma address is 80h
        CALL    SETDMA
;
        LD      A,(CDISK)       ;get current disk number
        LD      C,A             ;send to the ccp
        JP      CCP             ;go to cp/m for further processing
;
;
;       simple i/o handlers
;
;       console status, return 0ffh if character ready, 00h if not
;
CONST:  IN      A,(CONSTA)      ;get console status
        RET
;
;       console character into register a
;
CONIN:  IN      A,(CONDAT)      ;get character from console
        RET
;
;       console character output from register c
;
CONOUT: LD      A,C             ;get to accumulator
        OUT     (CONDAT),A      ;send character to console
        RET
;
;       list character from register c
;
LIST:   LD      A,C             ;character to register a
        OUT     (PRTDAT),A
        RET
;
;       return list status (00h if not ready, 0ffh if ready)
;
LISTST: IN      A,(PRTSTA)
        RET
;
;       punch character from register c
;
PUNCH:  LD      A,C             ;character to register a
        OUT     (AUXDAT),A
        RET
;
;       read character into register a from reader device
;
READER: IN      A,(AUXDAT)
        RET
;
;
;       i/o drivers for the disk follow
;
;       move to the track 00 position of current drive
;       translate this call into a settrk call with parameter 00
;
HOME:   LD      C,0             ;select track 0
        JP      SETTRK          ;we will move to 00 on first read/write
;
;       select disk given by register C
;
SELDSK: LD      HL,0000H        ;error return code
        LD      A,C
        CP      16              ;drive A: to P: ?
        JP      C,SELDISK       ;go
        RET                     ;no, error

;       disk number is in the proper range
;       compute proper disk parameter header address
SELDISK:
        OUT     (FDCD),A        ;select disk drive
        LD      L,A             ;L=disk number 0,1,2,3 ...
        ADD     HL,HL           ;*2
        ADD     HL,HL           ;*4
        ADD     HL,HL           ;*8
        ADD     HL,HL           ;*16 (size of each header)
        LD      DE,DPBASE
        ADD     HL,DE           ;HL=.dpbase(diskno*16)
        RET
;
;       set track given by register c
;
SETTRK: LD      A,C
        OUT     (FDCT),A
        RET
;
;       set sector given by register c
;
SETSEC: LD      A,C
        OUT     (FDCS),A
        RET
;
;       translate the sector given by BC using the
;       translate table given by DE
;
SECTRAN:
        LD      A,D             ;do we have a translation table?
        OR      E
        JP      NZ,SECT1        ;yes, translate
        LD      L,C             ;no, return untranslated
        LD      H,B             ;in HL
        INC     L               ;sector no. start with 1
        RET     NZ
        INC     H
        RET
SECT1:  EX      DE,HL           ;HL=.trans
        ADD     HL,BC           ;HL=.trans(sector)
        LD      L,(HL)          ;L = trans(sector)
        LD      H,0             ;HL= trans(sector)
        RET                     ;with value in HL
;
;       set dma address given by registers b and c
;
SETDMA: LD      A,C             ;low order address
        OUT     (DMAL),A
        LD      A,B             ;high order address
        OUT     (DMAH),A        ;in dma
        RET
;
;       perform read operation
;
READ:   XOR     A               ;read command -> A
        JP      WAITIO          ;to perform the actual i/o
;
;       perform a write operation
;
WRITE:  LD      A,1             ;write command -> A
;
;       enter here from read and write to perform the actual i/o
;       operation.  return a 00h in register a if the operation completes
;       properly, and 01h if an error occurs during the read or write
;
WAITIO: OUT     (FDCOP),A       ;start i/o operation
        IN      A,(FDCST)       ;status of i/o operation -> A
        RET
;
;       the remainder of the CBIOS is reserved uninitialized
;       data area, and does not need to be a part of the
;       system memory image (the space must be available,
;       however, between "begdat" and "enddat").
;
;       scratch ram area for BDOS use
;
BEGDAT  EQU     $               ;beginning of data area
DIRBF:  DEFS    128             ;scratch directory area

;allocation vectors for Multicomp 8 MB hard disks A: to P:
alv00:  DEFS    257
alv01:  DEFS    257
alv02:  DEFS    257
alv03:  DEFS    257
alv04:  DEFS    257
alv05:  DEFS    257
alv06:  DEFS    257
alv07:  DEFS    257
alv08:  DEFS    257
alv09:  DEFS    257
alv10:  DEFS    257
alv11:  DEFS    257
alv12:  DEFS    257
alv13:  DEFS    257
alv14:  DEFS    257
alv15:  DEFS    257
;
ENDDAT  EQU     $               ;end of data area
DATSIZ  EQU     $-BEGDAT        ;size of data area
;
        END                     ;of BIOS
