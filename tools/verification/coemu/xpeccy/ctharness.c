// Headless ctprobe runner on the upstream Xpeccy core (libxpeccy), no Qt.
//
// Upstream Xpeccy has no per-machine definition files: a machine is a profile, made from
// the template :/conf/xpeccy.conf (ZX48K, 48 KB, romset ZX48, reset to 48 BASIC, chip1 = 1)
// on top of compCreate()'s defaults, and the user then picks the hardware, the romset, the
// memory, the disk interface and the geometry (Setup window). This harness builds each
// machine the way xcore/profiles.cpp prf_load_conf() + prfSetRomset() + prfSetLayout() do
// for such a profile, with those choices made for the machine (the table below, and the
// README). Every timing option keeps its default unless the table says otherwise.
//
// It mounts the media with the core's own loaders (loadTAP / loadTRD into drive A), resets,
// waits for the ROM to settle, types the keys (hold 4 frames, then the key's gap) and, for
// a tape, presses Play (tapPlay). The tape then plays in real time into the ROM loader (no
// trap, no fast load); a disk is read by TR-DOS through the emulated FDC.
//
// usage: ctharness <machine> <romdir> <media> <sym> <outprefix> [maxframes] [option=value...]
//   machine: see the table (48k 128k plus2 plus2a plus3 pentagon scorpion profscorp atm710 atm3 profi)
//   romdir:  where the ROM files are (upstream ships only 1982.rom; see the README)
//   option:  a note run with one setting changed: evenm1=0|1, early=0|1, rom=<file> (a one-file romset),
//           settle=<frames>, keys=none|run|basic|k1,k2,... ("-" waits), hold=<frames> (for finding how a machine boots)
// exit: 0 DONE = 1, 1 not done in maxframes, 2 usage / setup error, 3 the program crashed
//       (the CPU reached #0000 after it started)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxpeccy/spectrum.h"
#include "libxpeccy/filetypes/filetypes.h"
#include "libxpeccy/cpu/Z80/z80.h"

typedef struct {
	const char* keys;
	int gap;
} asKey;

static int AS_KEY_HOLD = 4;
#define AS_KEY_GAP	8

#define ZK_ENTER	"E"
#define ZK_QUOTE	"Sp"
#define ZK_COLON	"Sz"
#define ZK_EXT		"CS"

// LOAD "" in 48 BASIC
static const asKey as_load[] = {
	{"j", AS_KEY_GAP}, {ZK_QUOTE, AS_KEY_GAP},
	{ZK_QUOTE, AS_KEY_GAP}, {ZK_ENTER, 0}, {NULL, 0}
};
// ENTER on the first item of the 128 / +2A / +3 menu (Tape Loader / Loader)
static const asKey as_menu[] = {
	{ZK_ENTER, 0}, {NULL, 0}
};
// RANDOMIZE USR 15619: REM: RUN, typed in 48 BASIC (TR-DOS runs "boot")
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
static const asKey as_none[] = {{NULL, 0}};
// ATM Turbo 2+: its BIOS menu (CP/M, TR-DOS 48, Spectrum 128, Spectrum 48, Turbo ON) comes up on reset.
// DOWN x4 and ENTER switch Turbo to OFF, UP x3 and ENTER boot the disk in TR-DOS 48
static const asKey as_atm_bios[] = {
	{"C6", 50}, {"C6", 50}, {"C6", 50}, {"C6", 50}, {ZK_ENTER, 50},
	{"C7", 50}, {"C7", 50}, {"C7", 50}, {ZK_ENTER, 0}, {NULL, 0}
};
// ZX-Evo: the EVO Reset Service menu comes up on reset. W twice (its stored 7.0 MHz -> 14 -> 3.5),
// Y (virtual drive A -> B, so that drive A is the emulated floppy), S (EVO-DOS), then RUN there
static const asKey as_evo_ers[] = {
	{"w", 50}, {"w", 50}, {"y", 50}, {"s", 150},
	{"r", 100}, {ZK_ENTER, 0}, {NULL, 0}
};
// RUN, typed in TR-DOS
static const asKey as_trdos_run[] = {
	{"r", AS_KEY_GAP}, {ZK_ENTER, 0}, {NULL, 0}
};

#define MEDIA_TAPE	0
#define MEDIA_DISK	1

// a romset entry, as in config.conf "rom = file:foffset:fsize:roffset" (KB; fsize 0 = the whole file)
typedef struct {
	const char* file;
	int foff;
	int fsize;
	int roff;
} xRom;

typedef struct {
	const char* id;		// the harness machine name
	const char* hw;		// upstream hardware name (libxpeccy/hardware/*.c)
	xRom roms[3];		// the romset
	const char* layout;	// geometry: name:full.x:full.y:bord.x:bord.y:blank.x:blank.y:intSize:intpos.y[:intpos.x]
	int contmem;		// [MACHINE] contmem (default no)
	int contio;		// [MACHINE] contio (default no)
	int contPattern;	// [VIDEO] contPattern (default CONT_PATA, "ULA type A")
	int early;		// [VIDEO] earlyTiming (default no)
	int disk;		// [DISK] type (default DIF_NONE)
	int resbank;		// [ROMSET] reset (template: basic48)
	int media;
	int settle;		// frames from reset until the ROM waits for keys
	const asKey* keys;
} xMac;

// the geometries upstream ships: config.cpp "default" and conf/config.conf "Pentagon", "Scorpion"
#define LAY_DEFAULT	"default:448:320:72:64:64:16:64:0:0"
#define LAY_PENT	"Pentagon:448:320:72:48:64:32:64:0"
#define LAY_SCORP	"Scorpion:448:312:48:48:80:32:64:32"
// upstream ships no Sinclair geometry: the user has to make one. These are the ones its fork
// xpeccy-plus ships for the same video engine (res/layouts.conf)
#define LAY_48		"ULA.48:448:312:64:56:64:16:64:8:120"
#define LAY_128		"ULA.128:456:311:48:48:104:23:72:8:148"
#define LAY_P3		"ULA.Plus3:456:311:64:48:64:16:64:1:118"

static const xMac macs[] = {
	{"48k", "ZX48K", {{"1982.rom", 0, 0, 0}}, LAY_48, 1, 1, CONT_PATA, 1, DIF_NONE, RES_48,
		MEDIA_TAPE, 200, as_load},
	{"128k", "Spectrum +2", {{"128.rom", 0, 0, 0}}, LAY_128, 1, 1, CONT_PATA, 1, DIF_NONE, RES_48,
		MEDIA_TAPE, 200, as_menu},
	{"plus2", "Spectrum +2", {{"plus2.rom", 0, 0, 0}}, LAY_128, 1, 1, CONT_PATA, 1, DIF_NONE, RES_48,
		MEDIA_TAPE, 200, as_menu},
	{"plus2a", "Spectrum +2", {{"plus2a.rom", 0, 0, 0}}, LAY_P3, 1, 1, CONT_PATB, 1, DIF_NONE, RES_48,
		MEDIA_TAPE, 250, as_menu},
	{"plus3", "Spectrum +3", {{"plus341.rom", 0, 0, 0}}, LAY_P3, 1, 1, CONT_PATB, 1, DIF_P3DOS, RES_48,
		MEDIA_TAPE, 250, as_menu},
	{"pentagon", "Pentagon", {{"128.rom", 0, 0, 0}, {"trdos503.rom", 0, 0, 48}}, LAY_PENT, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 200, as_trdos_basic},
	{"scorpion", "Scorpion", {{"scorpion.rom", 0, 0, 0}}, LAY_SCORP, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 200, as_trdos_basic},
	{"profscorp", "Scorpion", {{"scorp_prof401.rom", 0, 256, 0}}, LAY_SCORP, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 200, as_trdos_basic},
	{"atm710", "ATM2", {{"atm2.rom", 0, 0, 0}}, LAY_PENT, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 400, as_atm_bios},
	{"atm3", "PentEvo", {{"zxevo.rom", 0, 0, 0}}, LAY_PENT, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 300, as_evo_ers},
	{"profi", "Profi", {{"profi.rom", 0, 0, 0}}, LAY_DEFAULT, 0, 0, CONT_PATA, 0, DIF_BDI, RES_48,
		MEDIA_DISK, 200, as_trdos_basic},
	{NULL}
};

static vLayout parse_layout(const char* s) {
	vLayout l;
	int v[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 256, 192};
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

// profiles.cpp prfSetRomset
static int load_roms(Computer* comp, const char* dir, const xRom* roms) {
	int romsz = MEM_256;
	memset(comp->mem->romData, 0xff, MEM_512K);
	for (int i = 0; i < 3 && roms[i].file; i++) {
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s", dir, roms[i].file);
		FILE* f = fopen(path, "rb");
		if (!f) { fprintf(stderr, "can't open rom %s\n", path); return 0; }
		int foff = roms[i].foff * 1024;
		int roff = roms[i].roff * 1024;
		int fsze;
		if (roms[i].fsize <= 0) {
			fseek(f, 0, SEEK_END);
			fsze = (int)ftell(f);
		} else {
			fsze = roms[i].fsize * 1024;
		}
		if (roff + fsze > romsz) romsz = toPower(toLimits(roff + fsze, MEM_256, MEM_512K));
		if (roff + fsze > romsz) fsze = romsz - roff;
		fseek(f, foff, SEEK_SET);
		if (fread(comp->mem->romData + roff, fsze, 1, f) != 1) fprintf(stderr, "short read %s\n", path);
		fclose(f);
	}
	memSetSize(comp->mem, -1, romsz);
	vid_fnt_del(comp->vid);
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

// the other codes a key press carries (xcore/keymap.cpp keyMapInit): the ATM Turbo's keyboard modes
// (its BIOS menu) read the ATM code, not the ZX matrix
typedef struct {
	const char* zx;
	atmKey atm;
	int ps;
	int at;
	int xt;
} xKeyCodes;

static const xKeyCodes keyCodes[] = {
	{"1", {'1',0x31}, 0x16, 0x16, 0x02},
	{"2", {'2',0x32}, 0x1e, 0x1e, 0x03},
	{"3", {'3',0x33}, 0x26, 0x26, 0x04},
	{"4", {'4',0x34}, 0x25, 0x25, 0x05},
	{"5", {'5',0x35}, 0x2e, 0x2e, 0x06},
	{"6", {'6',0x45}, 0x36, 0x36, 0x07},
	{"7", {'7',0x44}, 0x3d, 0x3d, 0x08},
	{"8", {'8',0x43}, 0x3e, 0x3e, 0x09},
	{"9", {'9',0x42}, 0x46, 0x46, 0x0a},
	{"0", {'0',0x41}, 0x45, 0x45, 0x0b},
	{"q", {'Q',0x21}, 0x15, 0x15, 0x10},
	{"w", {'W',0x22}, 0x1d, 0x1d, 0x11},
	{"e", {'E',0x23}, 0x24, 0x24, 0x12},
	{"r", {'R',0x24}, 0x2d, 0x2d, 0x13},
	{"t", {'T',0x25}, 0x2c, 0x2c, 0x14},
	{"y", {'Y',0x55}, 0x35, 0x35, 0x15},
	{"u", {'U',0x54}, 0x3c, 0x3c, 0x16},
	{"i", {'I',0x53}, 0x43, 0x43, 0x17},
	{"o", {'O',0x52}, 0x44, 0x44, 0x18},
	{"p", {'P',0x51}, 0x4d, 0x4d, 0x19},
	{"a", {'A',0x11}, 0x1c, 0x1c, 0x1e},
	{"s", {'S',0x12}, 0x1b, 0x1b, 0x1f},
	{"d", {'D',0x13}, 0x23, 0x23, 0x20},
	{"f", {'F',0x14}, 0x2b, 0x2b, 0x21},
	{"g", {'G',0x15}, 0x34, 0x34, 0x22},
	{"h", {'H',0x65}, 0x33, 0x33, 0x23},
	{"j", {'J',0x64}, 0x3b, 0x3b, 0x24},
	{"k", {'K',0x63}, 0x42, 0x42, 0x25},
	{"l", {'L',0x62}, 0x4b, 0x4b, 0x26},
	{"E", {0x0d,0x61}, 0x5a, 0x5a, 0x1c},
	{"z", {'Z',0x02}, 0x1a, 0x1a, 0x2c},
	{"x", {'X',0x03}, 0x22, 0x22, 0x2d},
	{"c", {'C',0x04}, 0x21, 0x21, 0x2e},
	{"v", {'V',0x05}, 0x2a, 0x2a, 0x2f},
	{"b", {'B',0x75}, 0x32, 0x32, 0x30},
	{"n", {'N',0x74}, 0x31, 0x31, 0x31},
	{"m", {'M',0x73}, 0x3a, 0x3a, 0x32},
	{" ", {0x20,0x71}, 0x29, 0x29, 0x39},
	{"C6", {0x71,0x4d}, 0x60, 0x72e0, 0x50e0},
	{"C7", {0x70,0x4c}, 0x63, 0x75e0, 0x48e0},
	{NULL}
};

static void key(Computer* comp, const char* k, int press) {
	keyEntry ent;
	memset(&ent, 0, sizeof(ent));
	strncpy((char*)ent.zxKey, k, KEYSEQ_MAXLEN - 1);
	for (const xKeyCodes* c = keyCodes; c->zx; c++) {
		if (!strcmp(c->zx, k)) {
			ent.atmCode = c->atm;
			ent.psCode = c->ps;
			ent.atCode = c->at;
			ent.xtCode = c->xt;
			break;
		}
	}
	if (press) { if (comp->hw->keyp) comp->hw->keyp(comp, &ent); }
	else { if (comp->hw->keyr) comp->hw->keyr(comp, &ent); }
}

static long long frames = 0;
// the program's entry (START) and the first frame the CPU was seen there (checked after every instruction)
static int probeLo = -1;
static long long probeSeen = -1;
// the CPU at #0000 after the probe started: the program has crashed and will not set DONE
static long long crashAt = -1;
static void run_frame(Computer* comp) {
	int guard = 0;
	while (!comp->flgFRM && guard < 1000000) {
		compExec(comp);
		guard++;
		if ((probeSeen < 0) && (comp->cpu->regPC == probeLo)) probeSeen = frames;
		if ((probeSeen >= 0) && (crashAt < 0) && (comp->cpu->regPC == 0)) crashAt = frames;
	}
	comp->flgFRM = 0;
	frames++;
}

static int rd(Computer* comp, int adr) {
	return memRd(comp->mem, adr & 0xffff) & 0xff;
}

// the screen as text, against the 48 rom font at #3D00
static void dump_screen(Computer* comp, const unsigned char* font, const char* path) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	if ((comp->vid->vmode == VID_EVO_TEXT) || (comp->vid->vmode == VID_ATM_TEXT)) {
		// the ATM / ZX-Evo 80x25 text modes (video.c vidDrawATMtext / vidDrawEvoText): even columns at
		// #01C0 + row * 64 + col / 2, odd ones #2000 (ATM) or #1000 (Evo) further
		int evo = (comp->vid->vmode == VID_EVO_TEXT);
		int base = ((comp->vid->curscr + (evo ? 3 : 0)) << 14) + 0x1c0;
		for (int row = 0; row < 25; row++) {
			char line[81];
			for (int col = 0; col < 80; col++) {
				int adr = base + row * 64 + (col >> 1);
				if (col & 1) adr += evo ? 0x1000 : 0x2000;
				int c = comp->mem->ramData[adr];
				line[col] = (c >= 32 && c < 127) ? (char)c : ' ';
			}
			int n = 80;
			while (n > 0 && line[n - 1] == ' ') n--;
			line[n] = 0;
			fprintf(f, "%s\n", line);
		}
		fclose(f);
		return;
	}
	int scr = (comp->vid->curscr == 7) ? 7 : 5;
	for (int row = 0; row < 24; row++) {
		char line[33];
		for (int col = 0; col < 32; col++) {
			unsigned char cell[8];
			for (int k = 0; k < 8; k++) {
				int y = row * 8 + k;
				int adr = ((y & 0xc0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col;
				cell[k] = comp->mem->ramData[(scr << 14) + adr];
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

static void type_keys(Computer* comp, const asKey* keys) {
	for (int k = 0; keys[k].keys; k++) {
		if (!keys[k].keys[0]) {		// "": only wait
			for (int i = 0; i < keys[k].gap; i++) run_frame(comp);
			continue;
		}
		key(comp, keys[k].keys, 1);
		for (int i = 0; i < AS_KEY_HOLD; i++) run_frame(comp);
		key(comp, keys[k].keys, 0);
		for (int i = 0; i < keys[k].gap; i++) run_frame(comp);
	}
}

int main(int ac, char** av) {
	if (ac < 6) {
		fprintf(stderr, "usage: %s <machine> <romdir> <tap|trd> <sym> <outprefix> [maxframes] [option=value...]\n", av[0]);
		return 2;
	}
	const xMac* mac = macs;
	while (mac->id && strcmp(mac->id, av[1])) mac++;
	if (!mac->id) { fprintf(stderr, "unknown machine %s\n", av[1]); return 2; }
	const char* romdir = av[2];
	long long maxfr = (ac > 6) ? atoll(av[6]) : 100000;
	int evenm1 = 0;		// [MACHINE] scrp.wait ("Even M1"): compCreate default, not in the template
	int early = mac->early;
	int settle = mac->settle;
	const asKey* keys = mac->keys;
	const char* romOverride = NULL;	// a single-file romset instead of the table's
	for (int i = 7; i < ac; i++) {
		if (!strncmp(av[i], "evenm1=", 7)) evenm1 = atoi(av[i] + 7);
		else if (!strncmp(av[i], "early=", 6)) early = atoi(av[i] + 6);
		else if (!strncmp(av[i], "rom=", 4)) romOverride = av[i] + 4;
		else if (!strncmp(av[i], "settle=", 7)) settle = atoi(av[i] + 7);
		else if (!strncmp(av[i], "hold=", 5)) AS_KEY_HOLD = atoi(av[i] + 5);
		else if (!strcmp(av[i], "keys=none")) keys = as_none;
		else if (!strcmp(av[i], "keys=run")) keys = as_trdos_run;
		else if (!strcmp(av[i], "keys=basic")) keys = as_trdos_basic;
		else if (!strncmp(av[i], "keys=", 5)) {	// keys=k1,k2,...: each held, then 50 frames; "-" only waits
			static asKey custom[32];
			static char buf[256];
			int n = 0;
			strncpy(buf, av[i] + 5, sizeof(buf) - 1);
			for (char* t = strtok(buf, ","); t && n < 31; t = strtok(NULL, ",")) { custom[n].keys = strcmp(t, "-") ? t : ""; custom[n].gap = 50; n++; }
			custom[n].keys = NULL;
			keys = custom;
		}
		else { fprintf(stderr, "unknown option %s\n", av[i]); return 2; }
	}

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

	// profiles.cpp prfSetCurrent: compCreate + Dummy, then prf_load_conf with the template
	Computer* comp = compCreate();
	compSetHardware(comp, "Dummy");
	comp->resbank = mac->resbank;			// [ROMSET] reset
	chip_set_type(comp->ts->chipA, 1);		// [SOUND] chip1 = 1
	chip_set_type(comp->ts->chipB, SND_NONE);
	chip_set_type(comp->ts->chipC, SND_NONE);
	comp->gs->enable = 0;				// [SOUND] gs = no
	// the choices made for the machine
	comp->flgCNTM = mac->contmem;
	comp->flgCNTI = mac->contio;
	comp->vid->ula->conttype = mac->contPattern;
	comp->vid->ula->early = early;
	comp->flgEM1 = evenm1;
	difSetHW(comp->dif, mac->disk);
	if (!compSetHardware(comp, mac->hw)) { fprintf(stderr, "no hw %s\n", mac->hw); return 2; }
	xRom single[3] = {{romOverride, 0, 0, 0}, {NULL}, {NULL}};
	if (!load_roms(comp, romdir, romOverride ? single : mac->roms)) return 2;
	// [MACHINE] memory = 48 -> 64K, then the largest size the hardware takes when it does not take 64K
	int tmask = MEM_64K;
	if ((comp->hw->mask != 0) && (~comp->hw->mask & tmask)) {
		tmask = MEM_4M;
		while (!(comp->hw->mask & tmask) && tmask) tmask >>= 1;
	}
	memSetSize(comp->mem, tmask, -1);
	vLayout lay = parse_layout(mac->layout);
	comp_set_layout(comp, &lay);
	vid_set_border(comp->vid, 1.0);
	comp_kbd_release(comp);
	compReset(comp, RES_DEFAULT);
	if (comp->hw->id == HW_ZX48) comp->mem->ramMask = MEM_128K - 1;	// setupwin.cpp apply()

	printf("machine %s hw=\"%s\" ram=%dK rom=%s cpu=%.4fMHz contmem=%d contio=%d contPattern=%d earlyTiming=%d scrp.wait=%d 4t-border=%d disk=%d layout=%s\n",
		mac->id, comp->hw->name, comp->mem->ramSize >> 10, mac->roms[0].file, comp->cpuFrq, comp->flgCNTM, comp->flgCNTI,
		comp->vid->ula->conttype, comp->vid->ula->early, comp->flgEM1, comp->vid->brdstep == 7, comp->dif->type, mac->layout);

	// filer.cpp load_file: the image into the tape deck, or into drive A
	if (mac->media == MEDIA_TAPE) {
		if (loadTAP(comp, av[3], 0) != ERR_OK) { fprintf(stderr, "can't load tap %s\n", av[3]); return 2; }
	} else {
		int err = loadTRD(comp, av[3], 0);
		if (err != ERR_OK) { fprintf(stderr, "can't load trd %s (error %d)\n", av[3], err); return 2; }
	}

	for (int i = 0; i < settle; i++) run_frame(comp);
	type_keys(comp, keys);
	if (mac->media == MEDIA_TAPE) tapPlay(comp->tape);
	long long playFrame = frames;
	printf("keys typed at frame %lld%s\n", playFrame, (mac->media == MEDIA_TAPE) ? ", tape playing" : ", disk in drive A");

	long long startSeen = -1;
	while (frames < maxfr) {
		run_frame(comp);
		if ((startSeen < 0) && (probeSeen >= 0)) {
			startSeen = probeSeen;
			printf("probe code running at frame %lld; at the end of frame %lld: PC=#%04X IY=#%04X SP=#%04X IM=%d IFF1=%d\n",
				probeSeen, frames, comp->cpu->regPC, comp->cpu->regIY, comp->cpu->regSP, comp->cpu->regIM, comp->cpu->flgIFF1);
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
	printf("keyboard mode %d, video mode #%02X, screen page %d, PC=#%04X IFF1=%d IM=%d, code at PC:", comp->keyb->core ? comp->keyb->core->id : -1, comp->vid->vmode, comp->vid->curscr, comp->cpu->regPC,
		comp->cpu->flgIFF1, comp->cpu->regIM);
	for (int i = -6; i < 6; i++) printf(" %02X", rd(comp, comp->cpu->regPC + i));
	printf("\n");
	printf("wrote %s (#%04X..#%04X, %d bytes)\n", path, aSTART, aEND - 1, aEND - aSTART);

	// let the summary print, then the screen as text (the font from the 48 BASIC ROM, 1982.rom)
	for (int i = 0; i < 100; i++) run_frame(comp);
	unsigned char font[768];
	snprintf(path, sizeof(path), "%s/1982.rom", romdir);
	f = fopen(path, "rb");
	if (f) {
		fseek(f, 0x3d00, SEEK_SET);
		if (fread(font, 768, 1, f) == 1) {
			snprintf(path, sizeof(path), "%s.screen.txt", av[5]);
			dump_screen(comp, font, path);
			// and the raw screen (6912 bytes, a .scr)
			snprintf(path, sizeof(path), "%s.scr", av[5]);
			FILE* fs = fopen(path, "wb");
			if (fs) {
				fwrite(comp->mem->ramData + ((comp->vid->curscr == 7 ? 7 : 5) << 14), 6912, 1, fs);
				fclose(fs);
			}
		}
		fclose(f);
	}
	if (crashAt >= 0) return 3;
	return done == 1 ? 0 : 1;
}
