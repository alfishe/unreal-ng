; Plain Z80 source without sjasmplus-only directives: any text codec may read it
        ORG #8000
        LD A,2
        CALL #1601
        RET
