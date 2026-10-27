
;              KOMAHДЫ Xas


; Load   -3AГPУ3KA. edit - OTMEHA, ext -
;         NEW, cs/3 - ПEPEKЛЮЧ. CTPAHИЦ
;         KATAЛOГA (=CS/2 = CS/4), space -
;         ПEPE3AЧTEHИE KATAЛOГA.
; Save   -3AПИCЬ, ext - OTMEHA.
;    +cs -3AMEHA BCEX !OFF B TEKCTE HA
;         !ON, 3ATEM 3AПИCЬ.
; save cOde -3AПИCЬ OБЪEKTHOГO KOДA. ext -
;         OTMEHA.
; Cat    -KATAЛOГ. space - STOP.
;    +cs -BCE ЧИCЛA B ДECЯTИЧHOM BИДE.

; Quit   -BЫXOД B BASIC.
; sTs    -BЫXOД B STS.
;    +cs -BЫXOД B STS C УCTAHOBKOЙ memadr
;         = calc И pc = object run!
; Run    -3AПУCK OTACCEMБЛИPOBAHHOГO KOДA.
;    +cs -ПPИ BO3BPATE B xas ПOCЛE 3AПУCKA
;         ЭKPAH HE OЧИЩAETCЯ, ПOKA HE HA-
;         ЖATA KЛABИШA.
;    +ss -ПEPEД 3AПУCKOM PACKPУЧИBAHИE MO-
;         TOPA И ПO3ИЦИOHИPOBAHИE HA ДO-
;         POЖKУ И3 #5cf5.

; Mac edit -PEДAKTИPOBAHИE MAKPOCOB.
; aNother  -ПEPEKЛЮЧEHИE MEЖДУ TEKCTAMИ +
;         BЫXOД И3 PEДAKTИP. MAKPOCOB.
; neW    -УДAЛEHИE TEKCTA И3 ПAMЯTИ. ПPO-
;        CИT ПOДTBEPДИTЬ.

; Assemble -ACCEMБЛИPOBAHИE. ДEPЖATЬ cs
;        (ИЛИ break) - STOP.
;    +cs -ASSEMBLE, ECЛИ HE БЫЛO OШИБOK,
;        TO ABTOMATИЧECKИ BЫПOЛHЯETCЯ cs+
;        SAVE И RUN.
; laBels -ПPOCMOTP TAБЛИЦЫ METOK. space -
;        STOP.
;    +cs -B ДECЯTИЧHOM BИДE.
; lenGth -ПPOCMOTP TEKУЩEЙ MAKCИM. ДЛИHЫ
;        METOK И MAKCИM. BO3MOЖHOE KOЛ-BO
;        METOK.
;    +cs -TEKУЩAЯ ДЛИHA METKИ CTAHOBИTCЯ
;        = calc, ECЛИ OHO ЛEЖИT B ДИAПA3O-
;        HE 3..14, ПOЯBЛEHИE '*' - OK.

; Find   -ПOИCK CЛOBA. ext - ПOИCK CTAPOГO
;        CЛOBA. ; - ПOИCK CИMBOЛOB.
; comPute -KAЛЬKУЛЯTOP. ЛЮБЫE BЫPAЖEHИЯ,
;        METKИ, ПPOCMOTP 3HAЧEHИЙ METOK...
;        R - ПOCЛEДHEE 3HAЧEHИE.
;    +cs -BЫЧИCЛEHИE BЫPAЖEHИЯ, B HAЧAЛE
;        KOTOPOГO CTOИT KУPCOP B TEKCTE.
; coloUr -ЦBET OCH. ЭKPAHA CTAHOBИTCЯ PAB-
;        HЫM calc.
; wHole  -ИHBEPCИЯ OTMETKИ OT ПO3ИЦИИ KУP-
;        COPA ДO KOHЦA TEKCTA.
; remarK -PEДAKTИPOBAHИE KOMMEHTAPИЯ.
;
; Drive  -УCTAHOBKA ДИCKOBOДA ДЛЯ OПEPAЦИЙ
;        C ФAЙЛAMИ .




;           OПEPAЦИИ B CTPOKE


; edit  -HOME/END.
; caps lock -OVR/INS.
; graph -ПPEФИKC MAKPOCA.
; break -УДAЛEHИE ПO KУPCOPУ.
; cs/enter  -OTMETKA/PA3METKA KPACHЫM.
; ext   -KOMAHДHAЯ CTPOKA.
; ss/q  -PУC/LAT.
; ss/w  -УДAЛEHИE CTPOKИ.
; ss/e  -OTMETKA/PA3METKA ЖEЛTЫM.
; ss/a  -TAБУЛЯЦИЯ.
; ss/enter  -BOCCTAHOBЛEHИE CTPOKИ.
; cs/PУC -LAT.
; cs/LAT -lat.
; graph+enter - ПOИCK CЛOBA, HA KOTOPOM
;       CTOИT KУPCOP.


;           OПEPAЦИИ B TEKCTE


; cs3/4 -PGUP/PGDW.
; ss/y/u -ПOИCK ПOMEЧEHHOЙ CTPOKИ BBEPX/
;       /BHИ3.
; ss/i  -KOПИPOBAHИE/BCTABKA ЖEЛTЫX CTPOK
;       + KOПИPOBAHИE B БУФEP.
; ss/d  -УДAЛEHИE ЖEЛTЫX CTPOK.
; ss/s  -OTMEHA OTMETKИ.
; ss/f  -ПOИCK MOДEЛИ ДAЛЬШE.
; ss/g  -HAЧAЛO/KOHEЦ TEKCTA.
; graph+ss/i -KOПИPOBAHИE ЖEЛTЫX CTPOK B
;       БУФEP.


;              OCOБEHHOCTИ


; BPEMЯ ACCEMБЛИPOBAHИЯ = Tasm/7.
; !assm !on/!off .. !cont - ACCEMБЛИPOBATЬ
;   УЧACTOK OДИH PA3/HE ACCEMБЛИPOBATЬ BO-
;   OБЩE.
; !assm n .. !cont - ПOBTOPИTЬ ACCEMБЛИPO-
;   BAHИE УЧACTKA n PA3!!!
; ltext "filename" - 3AГPУ3KA И ACCEMБЛИ-
;   POBAHИE ФAЙЛA.
; lcode "filename" - 3AГPУ3KA KOДOBOГO
;   ФAЙЛA.
; usel "filename"  - 3AГPУ3KA И ACCEMБЛИ-
; :proc1           POBAHИE ПPOЦEДУP C ИME-
; :proc2 ...       HAMИ proc1 И 2 И3 БИБ-
;   ЛИOTEKИ "filename".
; "Text"... = defm "TKA И ACCEMБЛИ-
;   POBAHИE ФAЙЛA.
; lcode "filename" - 3AГPУ3KA KOДOBOГO
;   ФAЙЛA.
; usel "filename"  - 3AГPУ3KA И ACCEMБЛИ-
; :proc1           POBAHИE ПPOЦEДУP C ИME-
; :proc2 ...       HAMИ proc1 И 2 И3 БИБ-
;   ЛИOTEKИ "filename".
; "Text"... = defm "T.
; in/out lab,A = in/out (lab),A.
; PACKЛAД PУCCKИX KЛABИШ - 'ЯBEPTЫ' KAK B
;   isdos.


;            Xas 7.432 help


;OTЛИЧИЯ 7.1 OT 6.18: CS+A, SS+R,
; Bugs fix, PACKPУЧИBAHИE MOTOPA
; ГЛЮЧHЫX ДИCKOBOДOB.

;OTЛИЧИЯ 7.22 OT 7.1 BO3MOЖHOCTЬ 3AГPУЖATЬ
; LCODE C PACШИPEHИЯMИ OTЛИЧHЫMИ OT "C"
; ПPИMEP ИCПOЛЬ3OBAHИЯ :
        LCODE "CREATOR.Z"
; 3ДECЬ БУДET ДOГPУЖEH ФAИЛ "CREATOR" C
; PACШИPEHИEM "Z" .
;BHИMAHИE !!! ECЛИ BЫ HE ИCПOЛЬ3УETE
; PACШИPEHИE ( ПO УMOЛЧAHИЮ "C" ) И
; ПPEДПOCЛEДHИЙ CИMBOЛ "." TO HEOXOДИMO
; УKA3ATЬ ДЛЯ ДAHHOГO ФAЙЛA PACШИPEHИE
;ИCПOЛЬ3OBAHИE ДPУГOГO ДИCKOBOДA ДЛЯ
; 3AГPУ3KИ ФAЙЛOB ,ПPИMEP ИCПOЛЬ3OBAHИЯ:
        LCODE "B:FILENAME"
        LTEXT "B:FILENAME"
; 3ДECЬ БУДET 3AГPУЖEH ФAИЛ "FILENAME"
; C ДИCKA "B:"
; DEFB 3AMEHEH HA  DB ,TAK ЖE И DEFS - DS
; DEFM - DM !!!

; HAЧИHAЯ C 7.432  MOЖHO ПИCATЬ CTPИHГИ
; B DEFB , OCTOPOЖHEЙ! KABЫЧKИ B DEFB
; MOЖHO ПИCATЬ TOЛЬKO TAK :
; HAПPИMEP HAДO ПOЛУЧИTЬ "УPA!"
        DB    """
        DB    "УPA!"
        DB    """

; ЭTOT ФAЙЛ - COKPAЩEHHOE OПИCAHИE, KOTO-
; POE HE OCBOБOЖДAET BAC OT И3УЧEHИЯ ПOЛ-
; HOГO :)

;  ПOЛЬ3OBAHИE: cursor, cs/3/4, ss/g - HA-
; ЧAЛO/KOHEЦ TEKCTA, ss/y/u - XOДИTЬ ПO
; PA3ДEЛAM.

;* 'calc' O3HAЧAET 'ПOCЛEДHEE HAПEЧATAHHOE
; B KAЛЬKУЛЯTOPE ЧИCЛO'.

;------ Xas By Max Petrov (hpm)'97 -------
;====> Improved BY Creator product <======
