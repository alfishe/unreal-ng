; ports description and include file
;for NeoGS software projects, v0.3
;
; bits degisnation:
; B_* - bit position (0,1,2,3,4,5,6,7),
; M_* - bit mask (1,2,4,8,#10,#20,#40,#80)
; C_* - constants to be used
;
; part of NeoGS project
;
; (c) 2008 NedoPC

;ZX-side ports

GSCOM   EQU #BB                 ;write-only, command for NGS

GSSTAT  EQU #BB                 ;read-only, command and data bits
; (positions given immediately below)

B_CBIT  EQU 0                   ;Command   position
M_CBIT  EQU 1                   ;       BIT        and mask

B_DBIT  EQU 7                   ;Data   position
M_DBIT  EQU #80                 ;    BIT        and mask


GSDAT   EQU #B3                 ; read-write, data transfer register for NGS

GSCTR   EQU #33                 ; write-only, control register for NGS:
;constants available given immediately below

C_GRST  EQU #80                 ; reset constant to be written into GSCTR
C_GNMI  EQU #40                 ; NMI constant to be written into GSCTR
C_GLED  EQU #20                 ; LED toggle constant


;GS-side ports

MPAG    EQU #00                 ; write-only, Memory PAGe port
;(big pages at 8000-FFFF or small at 8000-BFFF)
MPAGEX  EQU #10                 ; write-only, Memory PAGe EXtended
;(only small pages at C000-FFFF)

ZXCMD   EQU #01                 ; read-only, ZX CoMmanD port: here is the byte
;written by ZX into GSCOM

ZXDATRD EQU #02                 ; read-only, ZX DATa ReaD: a byte written by ZX
;into GSDAT appears here;
; upon reading this port, data bit is cleared

ZXDATWR EQU #03                 ; write-only, ZX DATa WRite: a byte written here
;is available for ZX in GSDAT;
; upon writing here, data bit is set

ZXSTAT  EQU #04                 ; read-only, read ZX STATus:
;command and data bits.
;positions are defined by *_CBIT and *_DBIT above

CLRCBIT EQU #05                 ; read-write, upon either reading or writing
;this port, the Command BIT is CLeaRed

VOL1    EQU #06                 ; write-only, volumes for sound channels 1-8
VOL2    EQU #07
VOL3    EQU #08
VOL4    EQU #09
VOL5    EQU #16
VOL6    EQU #17
VOL7    EQU #18
VOL8    EQU #19

; following two ports are useless and very odd. They have been
;made just because they were on the original GS and for that
; strange case when somebody too crazy have used them.
;Nevertheless, DO NOT USE THEM! They can disappear or even
;radically change functionality in future firmware releases.
DAMNPORT1 EQU #0A               ; writing or reading this port sets data bit
;to the inverse of bit 0 into MPAG port
DAMNPORT2 EQU #0B               ; the same as DAMNPORT1, but instead command
;bit involved, which is made equal to 5th bit of VOL4

LEDCTR  EQU #01                 ; write-only, controls on-board LED.
;D0=0 - LED is on, D0=1 - LED is off
; reset state is LED on.

GSCFG0  EQU #0F                 ; read-write, GS ConFiG port 0:
;acts as memory cell, reads previously written
;value. Bits and fields follow:

B_NOROM EQU 0                   ; =0 - there is ROM everywhere except 4000-7FFF,
; =1 - the RAM is all around
M_NOROM EQU 1

B_RAMRO EQU 1                   ; =1 - ram absolute addresses 0000-7FFF
;(zeroth big page) are write-protected
M_RAMRO EQU 2

B_8CHANS EQU 2                  ; =1 - 8 channels mode
M_8CHANS EQU 4

B_EXPAG EQU 3                   ; =1 - extended paging: both MPAG and MPAGEX are
;used to switch two memory windows
M_EXPAG EQU 8

B_CKSEL0 EQU 4                  ;these bits should be set according to the
;C_**MHZ constants below
M_CKSEL0 EQU #10
B_CKSEL1 EQU 5
M_CKSEL1 EQU #20

C_10MHZ EQU #30
C_12MHZ EQU #10
C_20MHZ EQU #20
C_24MHZ EQU #00

B_PAN4CH EQU 6                  ; =1 - 4 channels, panning
;(every channel is on left and right with two volumes)
M_PAN4CH EQU #40

B_SETNCLR EQU 7
M_SETNCLR EQU #80


SCTRL   EQU #11                 ;Serial ConTRoL: read-write,
;read: current state of below bits,
;write - see GS_info

B_SDNCS EQU 0
M_SDNCS EQU 1

B_MCNCS EQU 1
M_MCNCS EQU 2

B_MPXRS EQU 2
M_MPXRS EQU 4

B_MCSPD0 EQU 3
M_MCSPD0 EQU 8

B_MDHLF EQU 4
M_MDHLF EQU #10

B_MCSPD1 EQU 5
M_MCSPD1 EQU #20


SSTAT   EQU #12                 ;Serial STATus:
;read-only, reads state of below bits

B_MDDRQ EQU 0
M_MDDRQ EQU 1

B_SDDET EQU 1
M_SDDET EQU 2

B_SDWP  EQU 2
M_SDWP  EQU 4

B_MCRDY EQU 3
M_MCRDY EQU 8


SD_SEND EQU #13                 ;SD card SEND, write-only,
;when written, byte transfer starts with written byte
SD_READ EQU #13                 ;SD card READ, read-only,
;reads byte received in previous byte transfer
SD_RSTR EQU #14                 ;SD card Read and STaRt, read-only, reads
;previously received byte and starts new byte transfer with #FF

MD_SEND EQU #14                 ;Mp3 Data SEND, write-only,
;sends byte to the mp3 data interface

MC_SEND EQU #15                 ;Mp3 Control SEND, write-only,
;sends byte to the mp3 control interface
MC_READ EQU #15                 ;Mp3 Control READ, read-only,
;reads byte that was received during previous sending of byte

DMA_MOD EQU #1B
DMA_HAD EQU #1C
DMA_MAD EQU #1D
DMA_LAD EQU #1E
DMA_CST EQU #1F

;Послать код команды в регистр команд
        MACRO SC _arg0
        LD A,_arg0
        OUT (GSCOM),A
        ENDM

;Ожидание сброса Command bit
        MACRO WC

.WCLP   IN A,(GSCOM)
        RRCA
        JR C,.WCLP

        ENDM

;Послать данные в регистр данных
        MACRO SD _arg0
        LD A,_arg0
        OUT (GSDAT),A
        ENDM

;Ожидание сброса Data bit
;по сути,ожидание,пока GS не примет посланные ему данные
        MACRO WD

.WDLP   IN A,(GSCOM)
        RLCA
        JR C,.WDLP

        ENDM

;Принять данные из регистра данных
        MACRO GD
        IN A,(GSDAT)
        ENDM

;Ожидание установки Data bit
;по сути,ожидание очередных данных от GS
        MACRO WN

.WNLP   IN A,(GSCOM)
        RLCA
        JR NC,.WNLP

        ENDM

