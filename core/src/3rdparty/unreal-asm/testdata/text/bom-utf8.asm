; Пример исходника: вывод строки на экран
        ORG #8000
START   LD HL,TEXT          ; адрес строки
        CALL PRINT          ; печать до нуля
        RET
PRINT   LD A,(HL)
        OR A
        RET Z
        RST #10             ; вывод символа через ПЗУ
        INC HL
        JR PRINT
TEXT    DB "Привет, Спектрум!",0
; конец файла
