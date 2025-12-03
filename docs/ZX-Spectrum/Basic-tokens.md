# Original table

|     | 00    | 10      | 20   | 30   | 40   | 50   | 60   | 70   | 80 (Block Graphics)              | 90    | A0     | B0    | C0    | D0       | E0       | F0         |
|:----|:------|:--------|:-----|:-----|:-----|:-----|:-----|:-----|:---------------------------------|:------|:-------|:------|:------|:---------|:---------|:-----------|
| **00**  |       | Ink     |      | 0    | @    | P    | £    | p    | Block GFX (all clear)            | UDG A | UDG Q  | VAL   | USR   | FORMAT   | LPRINT   | LIST       |
| **01**  |       | Paper   | !    | 1    | A    | Q    | a    | q    | Block GFX (bottom-left pixel)    | UDG B | UDG R  | LEN   | STR$  | MOVE     | LLIST    | LET        |
| **02**  |       | Flash   | "    | 2    | B    | R    | b    | r    | Block GFX (top-left pixel)       | UDG C | UDG S  | SIN   | CHR$  | ERASE    | STOP     | PAUSE      |
| **03**  |       | Bright  | #    | 3    | C    | S    | c    | s    | Block GFX (left vertical bar)    | UDG D | UDG T  | COS   | NOT   | OPEN #   | READ     | NEXT       |
| **04**  |       | Inverse | $    | 4    | D    | T    | d    | t    | Block GFX (bottom-right pixel)   | UDG E | UDG U  | TAN   | BIN   | CLOSE #  | DATA     | POKE       |
| **05**  |       | Over    | %    | 5    | E    | U    | e    | u    | Block GFX (bottom horizontal bar)| UDG F | RND    | ASN   | OR    | MERGE    | RESTORE  | PRINT      |
| **06**  | Tab   | At      | &    | 6    | F    | V    | f    | v    | Block GFX (diag TL-BR)           | UDG G | INKEY$ | ACS   | AND   | VERIFY   | NEW      | PLOT       |
| **07**  | (Tab) | Tab     | '    | 7    | G    | W    | g    | w    | Block GFX (all but top-right)    | UDG H | PI     | ATN   | <=    | BEEP     | BORDER   | RUN        |
| **08**  | Left  |         | (    | 8    | H    | X    | h    | x    | Block GFX (top-right pixel)      | UDG I | FN     | LN    | >=    | CIRCLE   | CONTINUE | SAVE       |
| **09**  | (Left)| Right   | )    | 9    | I    | Y    | i    | y    | Block GFX (diag BL-TR)           | UDG J | POINT  | EXP   | <>    | INK      | DIM      | RANDOMIZE  |
| **0A**  |       |         | *    | :    | J    | Z    | j    | z    | Block GFX (top horizontal bar)   | UDG K | SCREEN$| INT   | LINE  | PAPER    | REM      | IF         |
| **0B**  |       |         | +    | ;    | K    | [    | k    | {    | Block GFX (all but bottom-right) | UDG L | ATTR   | SQR   | THEN  | FLASH    | FOR      | CLS        |
| **0C**  |       |         | ,    | <    | L    | \    | l    | \|   | Block GFX (right vertical bar)   | UDG M | AT     | SGN   | TO    | BRIGHT   | GO TO    | DRAW       |
| **0D**  | CR    |         | -    | =    | M    | ]    | m    | }    | Block GFX (all but top-left)     | UDG N | TAB    | ABS   | STEP  | INVERSE  | GO SUB   | CLEAR      |
| **0E**  | (CR)  |         | .    | >    | N    | ^    | n    | ~    | Block GFX (all but bottom-left)  | UDG O | VAL$   | PEEK  | DEF FN| OVER     | INPUT    | RETURN     |
| **0F**  | (CR)  |         | /    | ?    | O    | _    | o    | ©    | Block GFX (all set)              | UDG P | CODE   | IN    | CAT   | OUT      | LOAD     | COPY       |

**Notes on merged cells:**
*   "Tab" in column 00, row 06 spans rows 06 and 07.
*   "Left" in column 00, row 08 spans rows 08 and 09.
*   "CR" in column 00, row 0D spans rows 0D, 0E, and 0F.
    (In the table above, `(Value)` indicates the continuation of a merged cell from the row above it).

**Notes on Block Graphics (Column 80):**
The descriptions refer to a 2x2 pixel matrix within the character cell:
*   Top-Left (TL), Top-Right (TR)
*   Bottom-Left (BL), Bottom-Right (BR)

# Transposed table

|      | **0**                       | **1**                         | **2**                        | **3**                         | **4**                          | **5**                             | **6**                        | **7**                         | **8**                       | **9**                         | **A**                        | **B**                          | **C**                        | **D**                          | **E**                         | **F**                       |
| :--- | :-------------------------- | :---------------------------- | :--------------------------- | :---------------------------- | :----------------------------- | :-------------------------------- | :--------------------------- | :---------------------------- | :-------------------------- | :---------------------------- | :--------------------------- | :----------------------------- | :--------------------------- | :----------------------------- | :---------------------------- | :-------------------------- |
| **0**  |                             |                               |                              |                               |                                |                                   | Tab                          | Tab                           | Left                        | Left                          |                              |                                |                              | CR                             | CR                            | CR                          |
| **1**  | Ink                         | Paper                         | Flash                        | Bright                        | Inverse                        | Over                              | At                           | Tab                           |                             | Right                         |                              |                                |                              |                                |                               |                             |
| **2**  |                             | !                             | "                            | #                             | $                              | %                                 | &                            | '                             | (                           | )                             | *                            | +                              | ,                            | -                              | .                             | /                           |
| **3**  | 0                           | 1                             | 2                            | 3                             | 4                              | 5                                 | 6                            | 7                             | 8                           | 9                             | :                            | ;                              | <                            | =                              | >                             | ?                           |
| **4**  | @                           | A                             | B                            | C                             | D                              | E                                 | F                            | G                             | H                           | I                             | J                            | K                              | L                            | M                              | N                             | O                           |
| **5**  | P                           | Q                             | R                            | S                             | T                              | U                                 | V                            | W                             | X                           | Y                             | Z                            | [                              | \                            | ]                              | ^                             | _                           |
| **6**  | £                           | a                             | b                            | c                             | d                              | e                                 | f                            | g                             | h                           | i                             | j                            | k                              | l                            | m                              | n                             | o                           |
| **70**  | p                           | q                             | r                            | s                             | t                              | u                                 | v                            | w                             | x                           | y                             | z                            | {                              | \|                           | }                              | ~                             | ©                           |
| **8**  | Block GFX (all clear)       | Block GFX (bottom-left pixel) | Block GFX (top-left pixel)   | Block GFX (left vertical bar) | Block GFX (bottom-right pixel) | Block GFX (bottom horizontal bar) | Block GFX (diag TL-BR)       | Block GFX (all but top-right) | Block GFX (top-right pixel) | Block GFX (diag BL-TR)        | Block GFX (top horizontal bar) | Block GFX (all but bottom-right) | Block GFX (right vertical bar) | Block GFX (all but top-left)   | Block GFX (all but bottom-left) | Block GFX (all set)         |
| **9**  | UDG A                       | UDG B                         | UDG C                        | UDG D                         | UDG E                          | UDG F                             | UDG G                        | UDG H                         | UDG I                       | UDG J                         | UDG K                        | UDG L                          | UDG M                        | UDG N                          | UDG O                         | UDG P                       |
| **A**  | UDG Q                       | UDG R                         | UDG S                        | UDG T                         | UDG U                          | RND                               | INKEY$                       | PI                            | FN                          | POINT                         | SCREEN$                      | ATTR                           | AT                           | TAB                            | VAL$                          | CODE                        |
| **B**  | VAL                         | LEN                           | SIN                          | COS                           | TAN                            | ASN                               | ACS                          | ATN                           | LN                          | EXP                           | INT                          | SQR                            | SGN                          | ABS                            | PEEK                          | IN                          |
| **C**  | USR                         | STR$                          | CHR$                         | NOT                           | BIN                            | OR                                | AND                          | <=                            | >=                          | <>                            | LINE                         | THEN                           | TO                           | STEP                           | DEF FN                        | CAT                         |
| **D**  | FORMAT                      | MOVE                          | ERASE                        | OPEN #                        | CLOSE #                        | MERGE                             | VERIFY                       | BEEP                          | CIRCLE                      | INK                           | PAPER                        | FLASH                          | BRIGHT                       | INVERSE                        | OVER                          | OUT                         |
| **E**  | LPRINT                      | LLIST                         | STOP                         | READ                          | DATA                           | RESTORE                           | NEW                          | BORDER                        | CONTINUE                    | DIM                           | REM                          | FOR                            | GO TO                        | GO SUB                         | INPUT                         | LOAD                        |
| **F**  | LIST                        | LET                           | PAUSE                        | NEXT                          | POKE                           | PRINT                             | PLOT                         | RUN                           | SAVE                        | RANDOMIZE                     | IF                           | CLS                            | DRAW                         | CLEAR                          | RETURN                        | COPY                        |


# Flat table

| HexCode | Value                         |
| :------ | :---------------------------- |
| **00**      |                               |
| **01**      |                               |
| **02**      |                               |
| **03**      |                               |
| **04**      |                               |
| **05**      |                               |
| **06**      | Tab                           |
| **07**      | Tab                           |
| **08**      | Left                          |
| **09**      | Left                          |
| **0A**      |                               |
| **0B**      |                               |
| **0C**      |                               |
| **0D**      | CR                            |
| **0E**      | CR                            |
| **0F**      | CR                            |
| **10**      | Ink                           |
| **11**      | Paper                         |
| **12**      | Flash                         |
| **13**      | Bright                        |
| **14**      | Inverse                       |
| **15**      | Over                          |
| **16**      | At                            |
| **17**      | Tab                           |
| **18**      |                               |
| **19**      | Right                         |
| **1A**      |                               |
| **1B**      |                               |
| **1C**      |                               |
| **1D**      |                               |
| **1E**      |                               |
| **1F**      |                               |
| **20**      |                               |
| **21**      | !                             |
| **22**      | "                             |
| **23**      | #                             |
| **24**      | $                             |
| **25**      | %                             |
| **26**      | &                             |
| **27**      | '                             |
| **28**      | (                             |
| **29**      | )                             |
| **2A**      | *                             |
| **2B**      | +                             |
| **2C**      | ,                             |
| **2D**      | -                             |
| **2E**      | .                             |
| **2F**      | /                             |
| **30**      | 0                             |
| **31**      | 1                             |
| **32**      | 2                             |
| **33**      | 3                             |
| **34**      | 4                             |
| **35**      | 5                             |
| **36**      | 6                             |
| **37**      | 7                             |
| **38**      | 8                             |
| **39**      | 9                             |
| **3A**      | :                             |
| **3B**      | ;                             |
| **3C**      | <                             |
| **3D**      | =                             |
| **3E**      | >                             |
| **3F**      | ?                             |
| **40**      | @                             |
| **41**      | A                             |
| **42**      | B                             |
| **43**      | C                             |
| **44**      | D                             |
| **45**      | E                             |
| **46**      | F                             |
| **47**      | G                             |
| **48**      | H                             |
| **49**      | I                             |
| **4A**      | J                             |
| **4B**      | K                             |
| **4C**      | L                             |
| **4D**      | M                             |
| **4E**      | N                             |
| **4F**      | O                             |
| **50**      | P                             |
| **51**      | Q                             |
| **52**      | R                             |
| **53**      | S                             |
| **54**      | T                             |
| **55**      | U                             |
| **56**      | V                             |
| **57**      | W                             |
| **58**      | X                             |
| **59**      | Y                             |
| **5A**      | Z                             |
| **5B**      | [                             |
| **5C**      | \                             |
| **5D**      | ]                             |
| **5E**      | ^                             |
| **5F**      | _                             |
| **60**      | £                             |
| **61**      | a                             |
| **62**      | b                             |
| **63**      | c                             |
| **64**      | d                             |
| **65**      | e                             |
| **66**      | f                             |
| **67**      | g                             |
| **68**      | h                             |
| **69**      | i                             |
| **6A**      | j                             |
| **6B**      | k                             |
| **6C**      | l                             |
| **6D**      | m                             |
| **6E**      | n                             |
| **6F**      | o                             |
| **70**      | p                             |
| **71**      | q                             |
| **72**      | r                             |
| **73**      | s                             |
| **74**      | t                             |
| **75**      | u                             |
| **76**      | v                             |
| **77**      | w                             |
| **78**      | x                             |
| **79**      | y                             |
| **7A**      | z                             |
| **7B**      | {                             |
| **7C**      | \|                            |
| **7D**      | }                             |
| **7E**      | ~                             |
| **7F**      | ©                             |
| **80**      | Block GFX (all clear)         |
| **81**      | Block GFX (bottom-left pixel) |
| **82**      | Block GFX (top-left pixel)    |
| **83**      | Block GFX (left vertical bar) |
| **84**      | Block GFX (bottom-right pixel)|
| **85**      | Block GFX (bottom horizontal bar)|
| **86**      | Block GFX (diag TL-BR)        |
| **87**      | Block GFX (all but top-right) |
| **88**      | Block GFX (top-right pixel)   |
| **89**      | Block GFX (diag BL-TR)        |
| **8A**      | Block GFX (top horizontal bar)|
| **8B**      | Block GFX (all but bottom-right)|
| **8C**      | Block GFX (right vertical bar)|
| **8D**      | Block GFX (all but top-left)  |
| **8E**      | Block GFX (all but bottom-left)|
| **8F**      | Block GFX (all set)           |
| **90**      | UDG A                         |
| **91**      | UDG B                         |
| **92**      | UDG C                         |
| **93**      | UDG D                         |
| **94**      | UDG E                         |
| **95**      | UDG F                         |
| **96**      | UDG G                         |
| **97**      | UDG H                         |
| **98**      | UDG I                         |
| **99**      | UDG J                         |
| **9A**      | UDG K                         |
| **9B**      | UDG L                         |
| **9C**      | UDG M                         |
| **9D**      | UDG N                         |
| **9E**      | UDG O                         |
| **9F**      | UDG P                         |
| **A0**      | UDG Q                         |
| **A1**      | UDG R                         |
| **A2**      | UDG S                         |
| **A3**      | UDG T                         |
| **A4**      | UDG U                         |
| **A5**      | RND                           |
| **A6**      | INKEY$                        |
| **A7**      | PI                            |
| **A8**      | FN                            |
| **A9**      | POINT                         |
| **AA**      | SCREEN$                       |
| **AB**      | ATTR                          |
| **AC**      | AT                            |
| **AD**      | TAB                           |
| **AE**      | VAL$                          |
| **AF**      | CODE                          |
| **B0**      | VAL                           |
| **B1**      | LEN                           |
| **B2**      | SIN                           |
| **B3**      | COS                           |
| **B4**      | TAN                           |
| **B5**      | ASN                           |
| **B6**      | ACS                           |
| **B7**      | ATN                           |
| **B8**      | LN                            |
| **B9**      | EXP                           |
| **BA**      | INT                           |
| **BB**      | SQR                           |
| **BC**      | SGN                           |
| **BD**      | ABS                           |
| **BE**      | PEEK                          |
| **BF**      | IN                            |
| **C0**      | USR                           |
| **C1**      | STR$                          |
| **C2**      | CHR$                          |
| **C3**      | NOT                           |
| **C4**      | BIN                           |
| **C5**      | OR                            |
| **C6**      | AND                           |
| **C7**      | <=                            |
| **C8**      | >=                            |
| **C9**      | <>                            |
| **CA**      | LINE                          |
| **CB**      | THEN                          |
| **CC**      | TO                            |
| **CD**      | STEP                          |
| **CE**      | DEF FN                        |
| **CF**      | CAT                           |
| **D0**      | FORMAT                        |
| **D1**      | MOVE                          |
| **D2**      | ERASE                         |
| **D3**      | OPEN #                        |
| **D4**      | CLOSE #                       |
| **D5**      | MERGE                         |
| **D6**      | VERIFY                        |
| **D7**      | BEEP                          |
| **D8**      | CIRCLE                        |
| **D9**      | INK                           |
| **DA**      | PAPER                         |
| **DB**      | FLASH                         |
| **DC**      | BRIGHT                        |
| **DD**      | INVERSE                       |
| **DE**      | OVER                          |
| **DF**      | OUT                           |
| **E0**      | LPRINT                        |
| **E1**      | LLIST                         |
| **E2**      | STOP                          |
| **E3**      | READ                          |
| **E4**      | DATA                          |
| **E5**      | RESTORE                       |
| **E6**      | NEW                           |
| **E7**      | BORDER                        |
| **E8**      | CONTINUE                      |
| **E9**      | DIM                           |
| **EA**      | REM                           |
| **EB**      | FOR                           |
| **EC**      | GO TO                         |
| **ED**      | GO SUB                        |
| **EE**      | INPUT                         |
| **EF**      | LOAD                          |
| **F0**      | LIST                          |
| **F1**      | LET                           |
| **F2**      | PAUSE                         |
| **F3**      | NEXT                          |
| **F4**      | POKE                          |
| **F5**      | PRINT                         |
| **F6**      | PLOT                          |
| **F7**      | RUN                           |
| **F8**      | SAVE                          |
| **F9**      | RANDOMIZE                     |
| **FA**      | IF                            |
| **FB**      | CLS                           |
| **FC**      | DRAW                          |
| **FD**      | CLEAR                         |
| **FE**      | RETURN                        |
| **FF**      | COPY                          |
