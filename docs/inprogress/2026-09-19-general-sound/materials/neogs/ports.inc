; ports description and include file for NeoGS software projects, v0.3
;
;
; bits degisnation: B_* - bit position (0,1,2,3,4,5,6,7), M_* - bit mask (1,2,4,8,#10,#20,#40,#80)
; C_* - constants to be used
;
;
; part of NeoGS project
;
; (c) 2008 NedoPC


;ZX-side ports

GSCOM   EQU     #BB ;write-only, command for NGS


GSSTAT	equ	#BB ;read-only, command and data bits (positions given immediately below)

B_CBIT	equ	0 ;Command   position
M_CBIT	equ	1 ;       BIT        and mask

B_DBIT	equ	7   ;Data   position
M_DBIT	equ	#80 ;    BIT        and mask


GSDAT   EQU     #B3 ; read-write, data transfer register for NGS

GSCTR   EQU     #33 ; write-only, control register for NGS: constants available given immediately below

C_GRST	equ	#80 ; reset constant to be written into GSCTR
C_GNMI	equ	#40 ; NMI constant to be written into GSCTR
C_GLED	equ	#20 ; LED toggle constant


;GS-side ports

MPAG	equ	#00 ; write-only, Memory PAGe port (big pages at 8000-FFFF or small at 8000-BFFF)
MPAGEX	equ	#10 ; write-only, Memory PAGe EXtended (only small pages at C000-FFFF)

ZXCMD	equ	#01 ; read-only, ZX CoMmanD port: here is the byte written by ZX into GSCOM

ZXDATRD	equ	#02 ; read-only, ZX DATa ReaD: a byte written by ZX into GSDAT appears here;
		    ; upon reading this port, data bit is cleared

ZXDATWR	equ	#03 ; write-only, ZX DATa WRite: a byte written here is available for ZX in GSDAT;
		    ; upon writing here, data bit is set

ZXSTAT	equ	#04 ; read-only, read ZX STATus: command and data bits. positions are defined by *_CBIT and *_DBIT above

CLRCBIT	equ	#05 ; read-write, upon either reading or writing this port, the Command BIT is CLeaRed


VOL1	equ	#06 ; write-only, volumes for sound channels 1-8
VOL2	equ	#07
VOL3	equ	#08
VOL4	equ	#09
VOL5	equ	#16
VOL6	equ	#17
VOL7	equ	#18
VOL8	equ	#19


; following two ports are useless and very odd. They have been made just because they were on the original GS and for that
; strange case when somebody too crazy have used them. Nevertheless, DO NOT USE THEM! They can disappear or even radically
; change functionality in future firmware releases.
DAMNPORT1	equ	#0A ; writing or reading this port sets data bit to the inverse of bit 0 into MPAG port
DAMNPORT2	equ	#0B ; the same as DAMNPORT1, but instead command bit involved, which is made equal to 5th bit of VOL4

LEDCTR		equ	#01 ; write-only, controls on-board LED. D0=0 - LED is on, D0=1 - LED is off
			    ; reset state is LED on.

GSCFG0	equ	#0F ; read-write, GS ConFiG port 0: acts as memory cell, reads previously written value. Bits and fields follow:

B_NOROM		equ	0 ; =0 - there is ROM everywhere except 4000-7FFF, =1 - the RAM is all around
M_NOROM		equ	1

B_RAMRO		equ	1 ; =1 - ram absolute addresses 0000-7FFF (zeroth big page) are write-protected
M_RAMRO		equ	2

B_8CHANS	equ	2 ; =1 - 8 channels mode
M_8CHANS	equ	4

B_EXPAG		equ	3 ; =1 - extended paging: both MPAG and MPAGEX are used to switch two memory windows
M_EXPAG		equ	8

B_CKSEL0	equ	4   ;these bits should be set according to the C_**MHZ constants below
M_CKSEL0	equ	#10
B_CKSEL1	equ	5
M_CKSEL1	equ	#20

C_10MHZ		equ	#30
C_12MHZ		equ	#10
C_20MHZ		equ	#20
C_24MHZ		equ	#00

B_PAN4CH	equ	6  ; =1 - 4 channels, panning (every channel is on left and right with two volumes)
M_PAN4CH	equ	#40

B_INV7B		equ	7 ; =1 - invert 7th bit of samples before putting them to MUL/DAC
M_INV7B		equ	#80





B_SETNCLR	equ	7
M_SETNCLR	equ	#80


SCTRL	EQU     #11   ;Serial ConTRoL: read-write, read: current state of below bits, write - see GS_info

B_SDNCS	equ	0
M_SDNCS	equ	1

B_MCNCS	equ	1
M_MCNCS	equ	2

B_MPXRS	equ	2
M_MPXRS	equ	4

B_MCSPD0 equ	3
M_MCSPD0 equ	8

B_MDHLF	equ	4
M_MDHLF	equ	#10

B_MCSPD1 equ	5
M_MCSPD1 equ	#20



SSTAT	EQU     #12   ;Serial STATus: read-only, reads state of below bits

B_MDDRQ	equ	0
M_MDDRQ	equ	1

B_SDDET	equ	1
M_SDDET	equ	2

B_SDWP	equ	2
M_SDWP	equ	4

B_MCRDY equ	3
M_MCRDY equ     8



SD_SEND EQU     #13 ;SD card SEND, write-only, when written, byte transfer starts with written byte
SD_READ EQU     #13 ;SD card READ, read-only, reads byte received in previous byte transfer
SD_RSTR EQU     #14 ;SD card Read and STaRt, read-only, reads previously received byte and starts new byte transfer with #FF

MD_SEND EQU     #14 ;Mp3 Data SEND, write-only, sends byte to the mp3 data interface

MC_SEND EQU     #15 ;Mp3 Control SEND, write-only, sends byte to the mp3 control interface
MC_READ EQU     #15 ;Mp3 Control READ, read-only, reads byte that was received during previous sending of byte



DMA_MOD	EQU	#1B ;select DMA module to work with via DMA_HAD, DMA_MAD, DMA_LAD, DMA_CST ports:

C_DMA_NONE	equ	0 ; none selected
C_DMA_ZX	equ	1 ; ZX module selected
C_DMA_SD	equ	2 ; SD module selected
C_DMA_MP3	equ	3 ; MP3 module selected


DMA_HAD	EQU	#1C ; High   DMA ADdress (bits 21:16)
DMA_MAD	EQU	#1D ; Middle DMA ADdress (bits 15:8 )
DMA_LAD	EQU	#1E ; Low    DMA ADdress (bits  7:0 )
DMA_CST	EQU	#1F ; DMA Control and STate



TIM_FRQ		equ	#0E ;timer frequency

C_DEF		equ	#00 ;default 37500 Hz (after reset)
C_DIV2		equ	#01 ;18750 Hz (div by 2)
C_DIV4		equ	#02 ;9375 Hz (div by 4)
C_DIV8		equ	#03 ; etc...
C_DIV16		equ	#04
C_DIV64		equ	#05
C_DIV256	equ	#06
C_DIV1024	equ	#07



INTENA		equ	#0C ;interrupt enables
INTREQ		equ	#0D ;interrupt requests

;bit 7 - B_SETNCLR or M_SETNCLR

B_MP3_DMA_INT	equ	2
M_MP3_DMA_INT	equ	#04

B_SD_DMA_INT	equ	1
M_SD_DMA_INT	equ	#02

B_TIMER_INT	equ	0
M_TIMER_INT	equ	#01


WIN0	equ	#20 ;0000-3fff 16k page (0..255 -- 0..4Mb)
WIN1	equ	#21 ;4000-7fff
WIN2	equ	#22 ;8000-bfff
WIN3	equ	#23 ;c000-ffff


