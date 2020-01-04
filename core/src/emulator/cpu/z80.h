#pragma once
#include "stdafx.h"
#include "sysdefs.h"

#include "emulator/platform.h"

#define Z80FAST __fastcall		// Try to keep function parameters in x86/x64 CPU registers. Not guaranteed for all compilers
#define Z80INLINE __forceinline // Time-critical inlines. Uses cross-compiler __forceinline macro from stdafx.h
#define Z80LOGIC uint8_t Z80FAST
#define Z80OPCODE void Z80FAST


#ifdef MOD_FASTCORE
   namespace z80fast
   {
   #include "z80_main.h"
   }
#else
   #define z80fast z80dbg
#endif

#ifdef MOD_DEBUGCORE
   namespace z80dbg
   {
   #include "z80_main.h"
   }
#else
   #define z80dbg z80fast
#endif

uint8_t Rm(uint32_t addr);
void Wm(uint32_t addr, uint8_t val);
uint8_t DbgRm(uint32_t addr);
void DbgWm(uint32_t addr, uint8_t val);

void reset(ROM_MODE mode);
void reset_sound(void);
void set_clk(void);


typedef void (Z80FAST *STEPFUNC)(Z80*);
typedef uint8_t(Z80FAST *LOGICFUNC)(Z80*, uint8_t byte);

typedef uint8_t(*TRm)(uint32_t addr);
typedef void(*TWm)(uint32_t addr, uint8_t val);

struct TMemIf
{
	TRm rm;
	TWm wm;
};

struct TZ80State
{
	union
	{
		unsigned tt;
		struct
		{
			unsigned t_l : 8;
			unsigned t : 24;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned pc;
		struct
		{
			uint8_t pcl;
			uint8_t pch;
		};
	};

	union
	{
		unsigned sp;
		struct
		{
			uint8_t spl;
			uint8_t sph;
		};
	};

	union
	{
		unsigned ir_;
		struct
		{
			uint8_t r_low;
			uint8_t i;
		};
	};

	union
	{
		unsigned int_flags;
		struct
		{
			uint8_t r_hi;
			uint8_t iff1;
			uint8_t iff2;
			uint8_t halted;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned bc;
		uint16_t bc16;
		struct
		{
			uint8_t c;
			uint8_t b;
		};
	};

	union
	{
		unsigned de;
		struct
		{
			uint8_t e;
			uint8_t d;
		};
	};

	union
	{
		unsigned hl;
		struct
		{
			uint8_t l;
			uint8_t h;
		};
	};

	union
	{
		unsigned af;
		struct
		{
			uint8_t f;
			uint8_t a;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned ix;
		struct
		{
			uint8_t xl;
			uint8_t xh;
		};
	};
	union
	{
		unsigned iy;
		struct
		{
			uint8_t yl;
			uint8_t yh;
		};
	};

	/*------------------------------*/
	struct
	{
		union
		{
			unsigned bc;
			struct
			{
				uint8_t c;
				uint8_t b;
			};
		};

		union
		{
			unsigned de;
			struct
			{
				uint8_t e;
				uint8_t d;
			};
		};
		union
		{
			unsigned hl;
			struct
			{
				uint8_t l;
				uint8_t h;
			};
		};
		union
		{
			unsigned af;
			struct
			{
				uint8_t f;
				uint8_t a;
			};
		};
	} alt;

	union
	{
		unsigned memptr; // undocumented register
		struct
		{
			uint8_t meml;
			uint8_t memh;
		};
	};

	uint8_t opcode;
	unsigned eipos, haltpos;

	/*------------------------------*/
	uint8_t im;
	bool nmi_in_progress;
	uint32_t tscache_addr[TS_CACHE_SIZE];
	uint8_t  tscache_data[TS_CACHE_SIZE];
};

struct Z80 : public TZ80State
{
	uint8_t tmp0, tmp1, tmp3;
	unsigned rate;
	bool vm1;	// halt handling type
	uint8_t outc0;
	uint16_t last_branch;
	unsigned trace_curs, trace_top, trace_mode;
	unsigned mem_curs, mem_top, mem_second;
	unsigned pc_trflags;
	unsigned nextpc;
	unsigned dbg_stophere;
	unsigned dbg_stopsp;
	unsigned dbg_loop_r1;
	unsigned dbg_loop_r2;
	uint8_t dbgchk; // Признак наличия активных брекпоинтов
	bool int_pend; // На входе int есть активное прерывание
	bool int_gate; // Разрешение внешних прерываний (1-разрешены/0 - запрещены)
	unsigned halt_cycle;

#define MAX_CBP 16
	unsigned cbp[MAX_CBP][128]; // Условия для условных брекпоинтов
	unsigned cbpn;

	int64_t debug_last_t; // used to find time delta
	uint32_t tpi; // Число тактов между прерываниями
	uint32_t trpc[40];
	//   typedef u8 (* TRmDbg)(u32 addr);
	//   typedef u8 *(* TMemDbg)(u32 addr);
	//   typedef void (* TWmDbg)(u32 addr, u8 val);
	typedef void(__cdecl *TBankNames)(int i, char *Name);
	typedef void (Z80FAST* TStep)();
	typedef int64_t(__cdecl * TDelta)();
	typedef void(__cdecl * TSetLastT)();
	//   TRmDbg DirectRm; // direct read memory in debuger
	//   TWmDbg DirectWm; // direct write memory in debuger
	//   TMemDbg DirectMem; // get direct memory pointer in debuger
	uint32_t Idx; // Индекс в массиве процессоров
	TBankNames BankNames;
	TStep Step;
	TDelta Delta;
	TSetLastT SetLastT;
	uint8_t *membits;
	uint8_t dbgbreak;
	const TMemIf *FastMemIf; // Быстрый интерфес памяти
	const TMemIf *DbgMemIf; // Интерфейс памяти для поддержки отладчика (брекпоинты на доступ к памяти)
	const TMemIf *MemIf; // Текущий активный интерфейс памяти

	void reset() { int_flags = ir_ = pc = 0; im = 0; last_branch = 0; int_pend = false; int_gate = true; }

	Z80(
		uint32_t Idx,
		TBankNames BankNames,
		TStep Step,
		TDelta Delta,
		TSetLastT SetLastT,
		uint8_t* membits,
		const TMemIf* FastMemIf,
		const TMemIf* DbgMemIf
		) :
		Idx(Idx),
		BankNames(BankNames),
		Step(Step),
		Delta(Delta),
		SetLastT(SetLastT),
		membits(membits),
		FastMemIf(FastMemIf),
		DbgMemIf(DbgMemIf)
	{
		MemIf = FastMemIf;
		tpi = 0;
		rate = (1 << 8);
		dbgbreak = 0;
		dbgchk = 0;
		debug_last_t = 0;
		trace_curs = trace_top = (unsigned)-1; trace_mode = 0;
		mem_curs = mem_top = 0;
		pc_trflags = nextpc = 0;
		dbg_stophere = dbg_stopsp = (unsigned)-1;
		dbg_loop_r1 = 0;
		dbg_loop_r2 = 0xFFFF;
		int_pend = false;
		int_gate = true;
		nmi_in_progress = false;
	}

	virtual ~Z80() { }
	
	uint32_t GetIdx() const { return Idx; }
	void SetTpi(uint32_t Tpi) { tpi = Tpi; }

	void SetFastMemIf() { MemIf = FastMemIf; }
	void SetDbgMemIf() { MemIf = DbgMemIf; }

	// Direct read memory in debugger
	uint8_t DirectRm(unsigned addr) const { return *DirectMem(addr); }

	// Direct write memory in debuger
	void DirectWm(unsigned addr, uint8_t val)
	{
		*DirectMem(addr) = val;
		uint16_t cache_pointer = addr & 0x1FF;
		tscache_addr[cache_pointer] = -1; // write invalidates flag
	}
	/*
	   virtual u8 rm(unsigned addr) = 0;
	   virtual void wm(unsigned addr, u8 val) = 0;
	   */
	virtual uint8_t *DirectMem(unsigned addr) const = 0; // get direct memory pointer in debuger

	virtual uint8_t rd(uint32_t addr)
	{
		tt += rate * 3;
		return MemIf->rm(addr);
	}

	virtual void wd(uint32_t addr, uint8_t val)
	{
		tt += rate * 3;
		MemIf->wm(addr, val);
	}

	virtual uint8_t in(unsigned port) = 0;
	virtual void out(unsigned port, uint8_t val) = 0;
	virtual uint8_t m1_cycle() = 0; // [vv] Не зависит от процессора (вынести в библиотеку)
	virtual uint8_t IntVec() = 0; // Функция возвращающая значение вектора прерывания для im2
	virtual void CheckNextFrame() = 0; // Проверка и обновления счетчика кадров и тактов внутри прерывания
	virtual void retn() = 0; // Вызывается в конце инструкции retn (должна сбрасывать флаг nmi_in_progress и обновлять раскладку памяти)
};

class TMainZ80 : public Z80
{
public:
	TMainZ80(
		uint32_t Idx,
		TBankNames BankNames,
		TStep Step,
		TDelta Delta,
		TSetLastT SetLastT,
		uint8_t *membits,
		const TMemIf *FastMemIf,
		const TMemIf *DbgMemIf
		) :
		Z80(Idx, BankNames, Step, Delta, SetLastT, membits, FastMemIf, DbgMemIf) { }
	/*
	   virtual u8 rm(unsigned addr) override;
	   virtual u8 dbgrm(unsigned addr) override;
	   virtual void wm(unsigned addr, u8 val) override;
	   virtual void dbgwm(unsigned addr, u8 val) override;
	*/
	virtual uint8_t *DirectMem(unsigned addr) const override; // get direct memory pointer in debuger

	uint8_t rd(uint32_t addr) override;

	virtual uint8_t m1_cycle() override;
	virtual uint8_t in(unsigned port) override;
	virtual void out(unsigned port, uint8_t val) override;
	virtual uint8_t IntVec() override;
	virtual void CheckNextFrame() override;
	virtual void retn() override;
};