#pragma once
#include "stdafx.h"

#define WORD4(a,b,c,d) (((unsigned)(a)) | (((unsigned)(b)) << 8) | (((unsigned)(c)) << 16) | (((unsigned)(d)) << 24))
#define WORD2(a,b) ((a) | ((b)<<8))
#define align_by(a,b) (((ULONG_PTR)(a) + ((b)-1)) & ~((b)-1))
#define hexdigit(a) ((a) < 'A' ? (a)-'0' : toupper(a)-'A'+10)

class Z80Assembler
{
public:
	uint8_t* disasm(uint8_t* cmd, unsigned current, char labels);
	int assemble(unsigned addr);
	int assemble_cmd(uint8_t *cmd, unsigned addr);

private:
	int ishex(char c)
	{
		return (isdigit(c) || (tolower(c) >= 'a' && tolower(c) <= 'f'));
	}

	uint8_t hex(char p)
	{
		p = tolower(p);
		return (p < 'a') ? p - '0' : p - 'a' + 10;
	}

	uint8_t hex(const char *p)
	{
		return 0x10 * hex(p[0]) + hex(p[1]);
	}
};
