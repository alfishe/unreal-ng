// Headless ctprobe runner on the xpeccy-plus core (libxpeccy), no Qt.
//
// Builds a machine exactly the way xcore/machines.cpp mac_from_def() +
// xm_set_roms() + xm_set_layout() + xm_set() do for the stock definitions in
// res/machines/*.conf and res/layouts.conf, mounts the media with the core's
// own loaders (loadTAP / loadTRD into drive A), and starts it the way
// xcore/autostart.cpp does: reset into the autostart bank (and, on a machine
// that boots a firmware, page as a snapshot would), wait until the rom scans
// the whole matrix, settle 150 frames, type the keys (hold 4 frames, then the
// key's own gap), then - for a tape - press Play (tapUserPlay). A tape is then
// played in real time into the ROM loader (no trap, no flash load); a disk is
// read by TR-DOS through the emulated FDC.
//
// usage: ctharness <machine> <romdir> <media> <sym> <outprefix> [maxframes]
//   machine: zx48 | zx128 | zxplus2 | zxplus2a | zxplus3 | pent | scorp | atm2 | profi | evo-baseconf
//   media:   the .tap for a tape machine, the .trd for a disk one (see the table)
// exit: 0 DONE = 1, 1 not done in maxframes, 3 the program crashed (the CPU reached #0000 after it started)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxpeccy/spectrum.h"
#include "libxpeccy/filetypes/filetypes.h"
#include "libxpeccy/cpu/Z80/z80.h"

// autostart.cpp asKey: keys pressed together, then the frames to wait after they are released
typedef struct {
	const char* keys;
	int gap;
} asKey;

#define AS_SETTLE	150
#define AS_KEY_HOLD	4
#define AS_KEY_GAP	8
#define AS_MENU_GAP	100
#define AS_GIVEUP	1500

#define ZK_ENTER	"E"
#define ZK_QUOTE	"Sp"
#define ZK_COLON	"Sz"
#define ZK_EXT		"CS"
#define ZK_DOWN		"C6"

// autostart.cpp: as_keyword / as_menu / as_trdos_basic / as_evo_disk
static const asKey as_keyword[] = {
	{"j", AS_KEY_GAP}, {ZK_QUOTE, AS_KEY_GAP},
	{ZK_QUOTE, AS_KEY_GAP}, {ZK_ENTER, 0}, {NULL, 0}
};
static const asKey as_menu[] = {
	{ZK_ENTER, 0}, {NULL, 0}
};
// RANDOMIZE USR 15619: REM: RUN, typed in 48 basic
static const asKey as_trdos_basic[] = {
	{"t", AS_KEY_GAP},
	{ZK_EXT, AS_KEY_GAP},
	{"l", AS_KEY_GAP},
	{"1", AS_KEY_GAP}, {"5", AS_KEY_GAP},
	{"6", AS_KEY_GAP}, {"1", AS_KEY_GAP},
	{"9", AS_KEY_GAP},
	{ZK_COLON, AS_KEY_GAP},
	{"e", AS_KEY_GAP},
	{ZK_COLON, AS_KEY_GAP},
	{"r", AS_KEY_GAP},
	{ZK_ENTER, 0}, {NULL, 0}
};
static const asKey as_evo_disk[] = {
	{"s", 0}, {NULL, 0}
};

#define MEDIA_TAPE	0
#define MEDIA_DISK	1

typedef struct {
	const char* id;		// res/machines/<id>.conf
	const char* hw;
	int memkb;		// [machine] memory
	int cpufrq;
	const char* turboSteps;	// [machine] cpu.turbo
	int resbank;		// [machine] reset
	int earback;		// [machine] issue
	int contio;
	int contmem;
	int scrpwait;		// [machine] scrp.wait
	const char* ramCold;	// [machine] ram.cold
	int ramNoise;		// [machine] ram.noise
	const char* layout;	// res/layouts.conf line for [video] geometry
	int contPattern;
	int early;
	int brd4t;
	int snow;
	int fbus;
	int psgCount;
	int psgType;
	int psgStereo;
	int sdrv;		// [sound] soundrive
	int disk;
	int ide;
	int mouse;
	int mouseWheel;
	int joy;		// 0 none, 1 kempston, 2 kempston8 (joy.buttons)
	int scantab;		// [input] kbd.scantab
	const char* roms[4];
	const char* font;	// [rom] font
	int media;		// what the program is loaded from
	int asRes;		// autostart.cpp as_mtab row for that media
	int asSnap;
	const asKey* asKeys;
} xMac;

#define LAY_48	"ULA.48:448:312:64:56:64:16:64:8:120:256:192"
#define LAY_128	"ULA.128:456:311:48:48:104:23:72:8:148:256:192"
#define LAY_P3	"ULA.Plus3:456:311:64:48:64:16:64:1:118:256:192"
#define LAY_PENT	"ULA.Pentagon:448:320:72:64:64:16:72:0:1:256:192"
#define LAY_SCORP	"ULA.Scorpion:448:312:48:48:80:24:72:8:128:256:192"
#define LAY_PROFI	"ULA.Profi:448:312:64:56:64:16:64:72:64:256:192"
#define LAY_ATM2	"ULA.ATM2:448:312:64:56:64:16:64:8:32:256:192"

#define PENT_COLD "ff*8 00*8 ff*8 00*8 ff*8 00*8 ff*8 00*8 00*8 ff*8 00*8 ff*8 00*8 ff*8 00*8 ff*8"

static const xMac macs[] = {
	// id, hw, mem, frq, turbo, reset, issue, contio, contmem, scrp.wait, ram.cold, ram.noise,
	// layout, contPattern, early, 4t-border, snow, floatbus,
	// psg count/type/stereo, soundrive, disk, ide, mouse, wheel, joy, scantab, roms, font,
	// media, autostart reset, snap, keys
	{"zx48", "ZX48", 64, 3500000, "1", RES_48, EAR_ISSUE3, 1, 1, 0, NULL, 0,
		LAY_48, 1, 1, 1, 1, FBUS_ULA,
		0, SND_AY, AY_MONO, SDRV_NONE, DIF_NONE, IDE_NONE, 0, 0, 0, 0,
		{"48.rom", NULL, NULL, NULL}, NULL,
		MEDIA_TAPE, RES_48, 0, as_keyword},
	{"zx128", "ZX128", 128, 3546900, "1", RES_128, EAR_ISSUE3, 1, 1, 0, NULL, 0,
		LAY_128, 1, 1, 1, 1, FBUS_ULA,
		1, SND_AY, AY_MONO, SDRV_NONE, DIF_NONE, IDE_NONE, 0, 0, 0, 0,
		{"128-0.rom", "128-1.rom", NULL, NULL}, NULL,
		MEDIA_TAPE, RES_128, 0, as_menu},
	// zxplus2.conf: inherit = zx128, other roms
	{"zxplus2", "ZX128", 128, 3546900, "1", RES_128, EAR_ISSUE3, 1, 1, 0, NULL, 0,
		LAY_128, 1, 1, 1, 1, FBUS_ULA,
		1, SND_AY, AY_MONO, SDRV_NONE, DIF_NONE, IDE_NONE, 0, 0, 0, 0,
		{"plus2-0.rom", "plus2-1.rom", NULL, NULL}, NULL,
		MEDIA_TAPE, RES_128, 0, as_menu},
	{"zxplus2a", "Plus2A", 128, 3546900, "1", RES_128, EAR_NONE, 0, 1, 0, NULL, 0,
		LAY_P3, 2, 1, 1, 0, FBUS_ASIC,
		1, SND_AY, AY_MONO, SDRV_NONE, DIF_NONE, IDE_NONE, 0, 0, 0, 0,
		{"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"}, NULL,
		MEDIA_TAPE, RES_128, 0, as_menu},
	{"zxplus3", "Plus3", 128, 3546900, "1", RES_128, EAR_NONE, 0, 1, 0, NULL, 0,
		LAY_P3, 2, 1, 1, 0, FBUS_ASIC,
		1, SND_AY, AY_MONO, SDRV_NONE, DIF_P3DOS, IDE_NONE, 0, 0, 0, 0,
		{"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"}, NULL,
		MEDIA_TAPE, RES_128, 0, as_menu},
	{"pent", "Pentagon", 128, 3500000, "1", RES_128, EAR_ISSUE3, 0, 0, 0, PENT_COLD, 18,
		LAY_PENT, 0, 0, 0, 0, FBUS_ATTR,
		1, SND_YM, AY_ABC, SDRV_COVOX, DIF_BDI, IDE_NONE, 1, 0, 2, 0,
		{"128p-0.rom", "128p-1.rom", "gluck.rom", "trdos504t.rom"}, NULL,
		MEDIA_DISK, RES_48, 0, as_trdos_basic},
	{"scorp", "Scorpion", 256, 3500000, "1,2", RES_128, EAR_ISSUE3, 0, 0, 1, NULL, 0,
		LAY_SCORP, 0, 0, 1, 0, FBUS_ATTR,
		1, SND_YM, AY_BAC, SDRV_COVOX, DIF_BDI, IDE_SMUC, 1, 0, 2, 0,
		{"256s-0.rom", "256s-1.rom", "256s-2.rom", "256s-3.rom"}, NULL,
		MEDIA_DISK, RES_48, 0, as_trdos_basic},
	{"atm2", "ATM2", 1024, 3500000, "1,2", RES_128, EAR_ISSUE3, 0, 0, 0, NULL, 0,
		LAY_ATM2, 0, 0, 0, 0, FBUS_ATTR,
		1, SND_YM, AY_ABC, SDRV_COVOX, DIF_BDI, IDE_ATM, 1, 0, 2, 0,
		{"atm2.rom", NULL, NULL, NULL}, "sgen.rom",
		MEDIA_DISK, RES_48, 1, as_trdos_basic},
	{"profi", "Profi", 1024, 3500000, "1,2", RES_128, EAR_ISSUE3, 0, 0, 0, NULL, 0,
		LAY_PROFI, 0, 0, 0, 0, FBUS_ATTR,
		1, SND_YM, AY_ACB, SDRV_COVOX, DIF_BDI, IDE_PROFI, 1, 0, 2, 0,
		{"profi.rom", NULL, NULL, NULL}, NULL,
		MEDIA_DISK, RES_48, 0, as_trdos_basic},
	// geometry ULA.Pentagon, 2 PSGs (TS)
	{"evo-baseconf", "Baseconf", 4096, 3500000, "1,2,4", RES_128, EAR_ISSUE3, 0, 0, 0, "aa55", 2,
		LAY_PENT, 0, 0, 0, 0, FBUS_ATTR,
		2, SND_YM, AY_ABC, SDRV_COVOX, DIF_BDI, IDE_NEMO_EVO, 1, 1, 2, KBD_AT,
		{"zxevo-fe.rom", NULL, NULL, NULL}, "sgen.rom",
		MEDIA_DISK, RES_128, 0, as_evo_disk},
	{NULL}
};

static vLayout parse_layout(const char* s) {
	vLayout l;
	int v[11] = {0};
	const char* p = strchr(s, ':');
	for (int i = 0; i < 11 && p; i++) {
		v[i] = atoi(p + 1);
		p = strchr(p + 1, ':');
	}
	l.full.x = v[0]; l.full.y = v[1];
	l.bord.x = v[2]; l.bord.y = v[3];
	l.blank.x = v[4]; l.blank.y = v[5];
	l.intSize = v[6];
	l.intpos.y = v[7]; l.intpos.x = v[8];
	l.scr.x = v[9]; l.scr.y = v[10];
	return l;
}

// xcore/common.cpp
static int toPower(int src) { int dst = 1; while (dst < src) dst <<= 1; return dst; }
static int toLimits(int src, int min, int max) { return (src < min) ? min : (src > max) ? max : src; }

// machines.cpp mac_ram_size
static int mac_ram_size(int kb, int mask) {
	int sz = kb ? kb : 64;
	sz = toLimits(toPower(sz << 10), MEM_256, MEM_4M);
	if ((mask != 0) && (~mask & sz)) {
		sz = MEM_4M;
		while (!(mask & sz) && sz) sz >>= 1;
	}
	return sz;
}

// machines.cpp mac_cold_ram: hex groups, each may carry *N
static void mac_cold_ram(Computer* comp, const char* pat, int noise) {
	if (!pat) return;
	unsigned char bytes[4096];
	int len = 0;
	const char* p = pat;
	while (*p) {
		while (*p == ' ') p++;
		if (!*p) break;
		unsigned char grp[64];
		int glen = 0;
		while (p[0] && p[0] != ' ' && p[0] != '*' && p[1] && p[1] != ' ' && p[1] != '*' && glen < 64) {
			char hx[3] = {p[0], p[1], 0};
			grp[glen++] = (unsigned char)strtol(hx, NULL, 16);
			p += 2;
		}
		int rep = 1;
		if (*p == '*') rep = (int)strtol(p + 1, (char**)&p, 10);
		while (*p && *p != ' ') p++;
		for (int r = 0; r < rep; r++)
			for (int i = 0; i < glen && len < (int)sizeof(bytes); i++) bytes[len++] = grp[i];
	}
	if (len > 0) mem_cold_fill(comp->mem, bytes, len, noise);
}

// machines.cpp mac_turbo_tab
static int mac_turbo_tab(const char* src, double* tab) {
	int cnt = 0;
	tab[cnt++] = 1.0;
	const char* p = src;
	while (p && *p) {
		char* e;
		double v = strtod(p, &e);
		if (e == p) break;
		if ((v >= 0.1) && (v <= 8.0) && (v > tab[cnt - 1]) && (cnt < TURBO_STEP_MAX)) tab[cnt++] = v;
		p = (*e == ',') ? e + 1 : e;
	}
	return cnt;
}

static int load_roms(Computer* comp, const char* dir, const char* const* roms) {
	int romsz = MEM_256;
	memset(comp->mem->romData, 0xff, MEM_512K);
	for (int i = 0; i < 4; i++) {
		if (!roms[i]) continue;
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s", dir, roms[i]);
		FILE* f = fopen(path, "rb");
		if (!f) { fprintf(stderr, "can't open rom %s\n", path); return 0; }
		fseek(f, 0, SEEK_END);
		int fsze = (int)ftell(f);
		rewind(f);
		int roff = i * MEM_16K;
		if (roff + fsze > romsz) romsz = toPower(toLimits(roff + fsze, MEM_256, MEM_512K));
		if (fread(comp->mem->romData + roff, fsze, 1, f) != 1) fprintf(stderr, "short read %s\n", path);
		fclose(f);
	}
	if (romsz < MEM_16K) romsz = MEM_16K;
	memSetSize(comp->mem, -1, romsz);
	comp_heat_sync(comp);
	return 1;
}

static int sym_get(const char* path, const char* name) {
	FILE* f = fopen(path, "r");
	if (!f) return -1;
	char line[256], nm[128], hex[64];
	int res = -1;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%127s equ %63s", nm, hex) == 2 && !strcmp(nm, name)) {
			res = (int)strtol(hex[0] == '#' ? hex + 1 : hex, NULL, 16);
			break;
		}
	}
	fclose(f);
	return res;
}

static void key(Computer* comp, const char* k, int press) {
	keyEntry ent;
	memset(&ent, 0, sizeof(ent));
	strncpy((char*)ent.zxKey, k, KEYSEQ_MAXLEN - 1);
	if (press) { if (comp->hw->keyp) comp->hw->keyp(comp, &ent); }
	else { if (comp->hw->keyr) comp->hw->keyr(comp, &ent); }
}

static long long frames = 0;
// the program's entry (START) and the first frame the CPU was seen there (checked after every instruction:
// sampling at the end of a frame can miss it, when every frame ends inside the ROM's interrupt handler)
static int probeLo = -1;
static long long probeSeen = -1;
static int probeSeenPC = 0;
// the CPU at #0000 after the probe started: the engine's RST 0 when an interrupt is not where it expects one,
// or a reset - either way the program has crashed and will not set DONE
static long long crashAt = -1;
static void run_frame(Computer* comp) {
	int guard = 0;
	while (!comp->flgFRM && guard < 1000000) {
		compExec(comp);
		guard++;
		if ((probeSeen < 0) && (comp->cpu->regPC == probeLo)) {
			probeSeen = frames;
			probeSeenPC = comp->cpu->regPC;
		}
		if ((probeSeen >= 0) && (crashAt < 0) && (comp->cpu->regPC == 0)) crashAt = frames;
	}
	comp->flgFRM = 0;
	frames++;
}

static int rd(Computer* comp, int adr) {
	return mem_page_rd(mem_get_page(comp->mem, adr & 0xffff), adr & 0xffff) & 0xff;
}

// the screen as text, against the 48 rom font at #3D00
static void dump_screen(Computer* comp, const unsigned char* font, const char* path) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int row = 0; row < 24; row++) {
		char line[33];
		for (int col = 0; col < 32; col++) {
			unsigned char cell[8];
			for (int k = 0; k < 8; k++) {
				int y = row * 8 + k;
				int adr = 0x4000 | ((y & 0xc0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col;
				cell[k] = rd(comp, adr);
			}
			char ch = '?';
			for (int c = 0; c < 96; c++) {
				if (!memcmp(cell, font + c * 8, 8)) { ch = (char)(32 + c); break; }
				int inv = 1;
				for (int k = 0; k < 8; k++) if ((unsigned char)~cell[k] != font[c * 8 + k]) { inv = 0; break; }
				if (inv) { ch = (char)(32 + c); break; }
			}
			line[col] = ch;
		}
		int n = 32;
		while (n > 0 && line[n - 1] == ' ') n--;
		line[n] = 0;
		fprintf(f, "%s\n", line);
	}
	fclose(f);
}

int main(int ac, char** av) {
	if (ac < 6) {
		fprintf(stderr, "usage: %s <machine> <romdir> <tap|trd> <sym> <outprefix> [maxframes]\n", av[0]);
		return 2;
	}
	const xMac* mac = macs;
	while (mac->id && strcmp(mac->id, av[1])) mac++;
	if (!mac->id) { fprintf(stderr, "unknown machine %s\n", av[1]); return 2; }
	const char* romdir = av[2];
	long long maxfr = (ac > 6) ? atoll(av[6]) : 100000;

	int aSTART = sym_get(av[4], "START");
	int aEND = sym_get(av[4], "PROBEEND");
	int aDONE = sym_get(av[4], "DONE");
	int aCLASS = sym_get(av[4], "CLASS");
	int aONSET = sym_get(av[4], "ONSET");
	int aCAPS = sym_get(av[4], "CAPS");
	int aFAILS = sym_get(av[4], "FAILS");
	probeLo = aSTART;
	// CLASS, ONSET, CAPS, FAILS: ctprobe's, printed when the program has them (turbotest has FAILS only)
	if (aSTART < 0 || aEND < 0 || aDONE < 0) {
		fprintf(stderr, "missing symbols in %s\n", av[4]);
		return 2;
	}

	// config.cpp conf_init: compCreate + Dummy; machines.cpp mac_from_def
	Computer* comp = compCreate();
	compSetHardware(comp, "Dummy");
	if (!compSetHardware(comp, mac->hw)) { fprintf(stderr, "no hw %s\n", mac->hw); return 1; }
	cpu_set_type(comp->cpu, "Z80", NULL, NULL);
	compSetBaseFrq(comp, mac->cpufrq / 1e6);
	comp->turboCount = mac_turbo_tab(mac->turboSteps, comp->turboTab);
	comp->turboStep = 1;
	int ramsz = mac_ram_size(mac->memkb, comp->hw->mask);
	memSetSize(comp->mem, ramsz, -1);
	mac_cold_ram(comp, mac->ramCold, mac->ramNoise);
	comp->resbank = mac->resbank;
	comp->earback = mac->earback;
	comp->fbus = mac->fbus;
	comp->flgCNTI = mac->contio;
	comp_set_cont(comp, mac->contmem);
	comp->flgEM1 = mac->scrpwait;
	comp->flgDDP = 0;
	comp->vid->ula->conttype = mac->contPattern;
	comp->vid->ula->early = mac->early;
	comp->vid->ula->enabled = 0;
	comp->vid->brdstep = mac->brd4t ? 7 : 1;
	comp_set_snow(comp, mac->snow);
	comp->flgSNOWX = 0;
	{	// mac_set_psg, psg.frq 0 (Auto)
		aymChip* psg[3] = {comp->ts->chipA, comp->ts->chipB, comp->ts->chipC};
		for (int i = 0; i < 3; i++) {
			psg[i]->stereo = mac->psgStereo;
			chip_set_type(psg[i], (i < mac->psgCount) ? mac->psgType : SND_NONE);
		}
		ts_set_frq(comp->ts, 0, comp->cpuFrq);
		comp->ts->type = (mac->psgCount > 2) ? TS_ZXNEXT : (mac->psgCount > 1) ? TS_NEDOPC : TS_NONE;
	}
	comp->gs->enable = 0;
	comp->saa->enabled = 0;
	comp->sdrv->type = mac->sdrv;
	difSetHW(comp->dif, mac->disk);
	difSetDrives(comp->dif, 4);
	ide_set_type(comp->ide, mac->ide);
	comp->mouse->enable = mac->mouse;
	comp->mouse->hasWheel = mac->mouseWheel;
	comp->joy->type = mac->joy ? XJ_KEMPSTON : XJ_NONE;
	comp->joy->extbuttons = (mac->joy == 2) ? 1 : 0;
	comp->keyb->pcmode = mac->scantab;
	// xm_set_roms: the roms, then the text-mode font
	if (!load_roms(comp, romdir, mac->roms)) return 1;
	if (mac->font) {
		char fpath[4096];
		snprintf(fpath, sizeof(fpath), "%s/%s", romdir, mac->font);
		vid_fnt_load(comp->vid, fpath);
	} else {
		vid_fnt_del(comp->vid);
	}
	vLayout lay = parse_layout(mac->layout);
	comp_set_layout(comp, &lay);
	vid_set_border(comp->vid, VID_BRD_FULL);
	comp_kbd_release(comp);
	compReset(comp, RES_DEFAULT);
	tapStop(comp->tape);
	tapRewind(comp->tape, 0);

	printf("machine %s hw=%s ram=%dK cpu=%d turbo=%s contio=%d contmem=%d contPattern=%d earlyTiming=%d 4t-border=%d snow=%d fbus=%d psg=%d disk=%d ide=%d layout=%s\n",
		mac->id, comp->hw->name, ramsz >> 10, mac->cpufrq, mac->turboSteps, comp->flgCNTI, comp->flgCNTM,
		comp->vid->ula->conttype, comp->vid->ula->early, mac->brd4t, comp->flgSNOW, comp->fbus,
		mac->psgCount, mac->disk, mac->ide, mac->layout);

	// filer.cpp load_file: the image into the tape deck, or into drive A
	if (mac->media == MEDIA_TAPE) {
		if (loadTAP(comp, av[3], 0) != ERR_OK) { fprintf(stderr, "can't load tap %s\n", av[3]); return 1; }
	} else {
		int err = loadTRD(comp, av[3], 0);
		if (err != ERR_OK) { fprintf(stderr, "can't load trd %s (error %d)\n", av[3], err); return 1; }
	}

	// autostart.cpp: autostart_arm + autostart_frame
	compUserReset(comp, mac->asRes);
	if (mac->asSnap) comp_snap_map(comp);
	comp->keyb->scanmask = 0;
	int life = AS_GIVEUP;
	while (comp->keyb->scanmask != 0xff) {
		run_frame(comp);
		if (--life < 0) { fprintf(stderr, "rom never scanned the keyboard\n"); return 1; }
	}
	printf("rom scans the keyboard at frame %lld\n", frames);
	for (int i = 0; i < AS_SETTLE; i++) run_frame(comp);
	for (int k = 0; mac->asKeys[k].keys; k++) {
		key(comp, mac->asKeys[k].keys, 1);
		for (int i = 0; i < AS_KEY_HOLD; i++) run_frame(comp);
		key(comp, mac->asKeys[k].keys, 0);
		for (int i = 0; i < mac->asKeys[k].gap; i++) run_frame(comp);
	}
	if (mac->media == MEDIA_TAPE) tapUserPlay(comp->tape);
	long long playFrame = frames;
	printf("keys typed at frame %lld%s\n", playFrame, (mac->media == MEDIA_TAPE) ? ", tape playing" : ", disk in drive A");

	long long startSeen = -1;
	while (frames < maxfr) {
		run_frame(comp);
		if ((startSeen < 0) && (probeSeen >= 0)) {
			startSeen = probeSeen;
			printf("probe code running (PC=#%04X) at frame %lld; at the end of frame %lld: PC=#%04X IY=#%04X SP=#%04X IM=%d IFF1=%d\n",
				probeSeenPC, probeSeen, frames, comp->cpu->regPC, comp->cpu->regIY, comp->cpu->regSP, comp->cpu->regIM, comp->cpu->flgIFF1);
		}
		if (startSeen >= 0 && rd(comp, aDONE) == 1) break;
		if (crashAt >= 0) {
			printf("the program crashed: the CPU reached #0000 at frame %lld\n", crashAt);
			break;
		}
		if ((frames % 5000) == 0) printf("  frame %lld PC=#%04X\n", frames, comp->cpu->regPC);
	}
	int done = rd(comp, aDONE);
	printf("stopped at frame %lld (%lld after the keys, %lld after probe start)\n", frames, frames - playFrame,
		startSeen >= 0 ? frames - startSeen : -1);

	char path[4096];
	snprintf(path, sizeof(path), "%s.bin", av[5]);
	FILE* f = fopen(path, "wb");
	if (f) {
		for (int a = aSTART; a < aEND; a++) fputc(rd(comp, a), f);
		fclose(f);
	}
	printf("DONE=%d", done);
	if (aCLASS >= 0) printf(" CLASS=%d", rd(comp, aCLASS));
	if (aONSET >= 0) printf(" ONSET=%d", rd(comp, aONSET) | (rd(comp, aONSET + 1) << 8));
	if (aCAPS >= 0) printf(" CAPS=#%02X", rd(comp, aCAPS));
	if (aFAILS >= 0) printf(" FAILS=%d", rd(comp, aFAILS) | (rd(comp, aFAILS + 1) << 8));
	printf("\n");
	printf("wrote %s (#%04X..#%04X, %d bytes)\n", path, aSTART, aEND - 1, aEND - aSTART);

	// let the summary print, then the screen as text
	for (int i = 0; i < 100; i++) run_frame(comp);
	unsigned char font[768];
	snprintf(path, sizeof(path), "%s/48.rom", romdir);
	f = fopen(path, "rb");
	if (f) {
		fseek(f, 0x3d00, SEEK_SET);
		if (fread(font, 768, 1, f) == 1) {
			snprintf(path, sizeof(path), "%s.screen.txt", av[5]);
			dump_screen(comp, font, path);
		}
		fclose(f);
	}
	// no compDestroy(): it aborts on the way out (not needed for a one-shot run)
	if (crashAt >= 0) return 3;
	return done == 1 ? 0 : 1;
}
