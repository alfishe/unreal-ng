; =====================================================================================================================
; SAVEOBJ4 - The Link (Alone Coder, 2009): the author's ALASM object saver, INCLUDEd at the end of every effect unit.
; Converted from ALASM to sjasmplus; the comments without "en:" are the author's.
;
; In ALASM the author assembled a unit and RAN it at nenado (the DISPLAY lines print the address and the hint
; "RUN[CS/Ent]"): with CAPS SHIFT held it saves every object of the unit's ObjTab to the TR-DOS disk; without it,
; it jumps to the unit's GO (the stand-alone test run). A unit that defines the label "make" saves at once.
; In the sjasmplus build these bytes are still assembled after ObjTab, but nothing calls them: the SAVEBIN block at
; the end of each unit writes the same objects.
;
; ObjTab entry (16 bytes): name 8 + type 1, address (DW), length (DW), #7FFD page (DB), start or 2 more type letters
; (DW). The table ends with a 0 byte - the NOP at the start of this file does that when ObjTab precedes it.
; Per object: name to #5CDD, page in; TR-DOS #3D13 C=#0A find, C=#12 delete if found; C=#0B save HL=address,
; DE=length; find again, put the last word into the descriptor's start field (#5CE6), C=#09 write the descriptor.
;
; Converter change: ALASM's "IFN ?make" became "IF !exist make".
; =====================================================================================================================
;Object Saver v4.0 ЦаПрЮгА 12vi5 Alone
; en: Object Saver v4.0 (then the author's tag; 12vi5: probably 12 June 2005) Alone

; en: GO = where to go without CAPS
;GO=куда идти без CAPS

; en: the project must have just compiled the table
;     DB "filenameE (name and type letter), DW begin in mem, DW len, DB page, DW start / 2 more extension letters
;проект должен только что скомпилировать таблицу
;ORG ObjTab
;DB "имяфайлаE
;DW begin in mem
;DW len
;DB page
;DW start/ещё_2_буквы_расширения
;...
; en: the NOP is the 0 that ends a preceding ObjTab
        NOP
        ORG $
nenado
; en: CALL 8026 = #1F5A in the 48K ROM: reads the CAPS SHIFT half-row, carry set = CAPS not pressed -> GO
        IF !exist make
        CALL 8026
        JP C,GO
        ENDIF
; en: walk ObjTab; per entry: name + type to TR-DOS's #5CDD, address, length, page
        LD HL,ObjTab
_SVNEXT LD A,(HL)
        OR A
        RET Z
        LD DE,#5CDD
        LD BC,9
        LDIR
        LD E,(HL)
        INC HL
        LD D,(HL)
        INC HL
        PUSH DE
        LD E,(HL)
        INC HL
        LD D,(HL)
        INC HL
        LD A,(HL)
        INC HL
; en: the object's page in at #C000
        LD BC,#7FFD
        OUT (C),A
        EX (SP),HL              ;HL=begin
        PUSH HL
        PUSH DE
; en: find the file (C=#0A); C returns its number or #FF. INC C sets Z only for #FF, LD C keeps the flags,
;     so an existing file is deleted (C=#12) before the save
        LD C,10
        CALL #3D13
        INC C
        LD C,18
        CALL NZ,#3D13
        POP DE
        POP HL
; en: save CODE (C=#0B): HL = address, DE = length
        LD C,11
        CALL #3D13

        LD C,10                 ;find desc (en: find the descriptor)
        CALL #3D13
; en: the last word of the entry -> the start field of the descriptor at #5CDD
        POP HL
        LD E,(HL)
        INC HL
        LD D,(HL)
        INC HL
        PUSH HL
        LD (#5CE6),DE
        LD A,C
        LD C,9                  ;save desc (en: save the descriptor, A = its number from the find)
        CALL #3D13
        POP HL
        JR _SVNEXT
        DISPLAY 'Saver:',nenado,'-',$
        DISPLAY 'RUN[CS/Ent]'
