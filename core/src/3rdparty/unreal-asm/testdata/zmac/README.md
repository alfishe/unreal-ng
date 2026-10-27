# zmac test sources

Written for the unreal-asm zmac frontend (no third-party code): each file exercises a group of constructs of the zmac
dialect and is assembled by the real zmac (`zmac -z --oo cim`) and, converted, by sjasmplus; the bytes must be equal
(`tools/verification/unreal-asm/checks/dialectcheck.py zmac`).
