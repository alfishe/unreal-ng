// Headless ctprobe runner on the xpeccy-plus core (libxpeccy), no Qt.
//
// Builds a machine exactly the way xcore/machines.cpp mac_from_def() +
// xm_set_roms() + xm_set_layout() + xm_set() do for the stock definitions in
// res/machines/*.conf and res/layouts.conf, loads the .tap with the core's own
// loadTAP(), and starts it the way xcore/autostart.cpp does: reset into the
// autostart bank, wait until the rom scans the whole matrix, settle 150 frames,
// type the keys (hold 4, gap 8 frames), then press Play (tapUserPlay). The tape
// is then played in real time into the ROM loader (no trap, no flash load).
//
// usage: ctharness <machine> <romdir> <tap> <sym> <outprefix> [maxframes]
//   machine: zx48 | zx128 | zxplus2a | zxplus3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxpeccy/spectrum.h"
#include "libxpeccy/filetypes/filetypes.h"
#include "libxpeccy/cpu/Z80/z80.h"

typedef struct {
	const char* id;
	const char* hw;
	int ram;
	int cpufrq;
	int resbank;		// [machine] reset
	int earback;		// [machine] issue
	int contio;
	int contmem;
	const char* layout;	// res/layouts.conf line for [video] geometry
	int contPattern;
	int early;
	int brd4t;
	int snow;
	int fbus;
	int psgCount;
	int disk;
	const char* roms[4];
	int asRes;		// autostart.cpp as_mtab tape row
	const char* const* asKeys;
} xMac;

// autostart.cpp: as_keyword / as_menu
static const char* const ks_keyword[] = {"j", "Sp", "Sp", "E", NULL};
static const char* const ks_menu[] = {"E", NULL};

static const xMac macs[] = {
	{"zx48", "ZX48", MEM_64K, 3500000, RES_48, EAR_ISSUE3, 1, 1,
		"ULA.48:448:312:64:56:64:16:64:8:120:256:192", 1, 1, 1, 1, FBUS_ULA, 0, DIF_NONE,
		{"48.rom", NULL, NULL, NULL}, RES_48, ks_keyword},
	{"zx128", "ZX128", MEM_128K, 3546900, RES_128, EAR_ISSUE3, 1, 1,
		"ULA.128:456:311:48:48:104:23:72:8:148:256:192", 1, 1, 1, 1, FBUS_ULA, 1, DIF_NONE,
		{"128-0.rom", "128-1.rom", NULL, NULL}, RES_128, ks_menu},
	{"zxplus2a", "Plus2A", MEM_128K, 3546900, RES_128, EAR_NONE, 0, 1,
		"ULA.Plus3:456:311:64:48:64:16:64:1:118:256:192", 2, 1, 1, 0, FBUS_ASIC, 1, DIF_NONE,
		{"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"}, RES_128, ks_menu},
	{"zxplus3", "Plus3", MEM_128K, 3546900, RES_128, EAR_NONE, 0, 1,
		"ULA.Plus3:456:311:64:48:64:16:64:1:118:256:192", 2, 1, 1, 0, FBUS_ASIC, 1, DIF_P3DOS,
		{"plus3-0.rom", "plus3-1.rom", "plus3-2.rom", "plus3-3.rom"}, RES_128, ks_menu},
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
static void run_frame(Computer* comp) {
	int guard = 0;
	while (!comp->flgFRM && guard < 1000000) {
		compExec(comp);
		guard++;
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
		fprintf(stderr, "usage: %s <machine> <romdir> <tap> <sym> <outprefix> [maxframes]\n", av[0]);
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
	if (aSTART < 0 || aEND < 0 || aDONE < 0 || aCLASS < 0 || aONSET < 0 || aCAPS < 0 || aFAILS < 0) {
		fprintf(stderr, "missing symbols in %s\n", av[4]);
		return 2;
	}

	// config.cpp conf_init: compCreate + Dummy; machines.cpp mac_from_def
	Computer* comp = compCreate();
	compSetHardware(comp, "Dummy");
	if (!compSetHardware(comp, mac->hw)) { fprintf(stderr, "no hw %s\n", mac->hw); return 1; }
	cpu_set_type(comp->cpu, "Z80", NULL, NULL);
	compSetBaseFrq(comp, mac->cpufrq / 1e6);
	comp->turboStep = 1;
	memSetSize(comp->mem, mac->ram, -1);
	comp->resbank = mac->resbank;
	comp->earback = mac->earback;
	comp->fbus = mac->fbus;
	comp->flgCNTI = mac->contio;
	comp_set_cont(comp, mac->contmem);
	comp->flgEM1 = 0;
	comp->flgDDP = 0;
	comp->vid->ula->conttype = mac->contPattern;
	comp->vid->ula->early = mac->early;
	comp->vid->ula->enabled = 0;
	comp->vid->brdstep = mac->brd4t ? 7 : 1;
	comp_set_snow(comp, mac->snow);
	comp->flgSNOWX = 0;
	{
		aymChip* psg[3] = {comp->ts->chipA, comp->ts->chipB, comp->ts->chipC};
		for (int i = 0; i < 3; i++) {
			psg[i]->stereo = AY_MONO;
			chip_set_type(psg[i], (i < mac->psgCount) ? SND_AY : SND_NONE);
		}
		ts_set_frq(comp->ts, 0, comp->cpuFrq);
		comp->ts->type = TS_NONE;
	}
	comp->gs->enable = 0;
	comp->saa->enabled = 0;
	comp->sdrv->type = SDRV_NONE;
	difSetHW(comp->dif, mac->disk);
	difSetDrives(comp->dif, 4);
	ide_set_type(comp->ide, IDE_NONE);
	comp->mouse->enable = 0;
	comp->mouse->hasWheel = 0;
	comp->joy->type = XJ_NONE;
	comp->joy->extbuttons = 0;
	comp->keyb->pcmode = 0;
	if (!load_roms(comp, romdir, mac->roms)) return 1;
	vLayout lay = parse_layout(mac->layout);
	comp_set_layout(comp, &lay);
	vid_set_border(comp->vid, VID_BRD_FULL);
	comp_kbd_release(comp);
	compReset(comp, RES_DEFAULT);
	tapStop(comp->tape);
	tapRewind(comp->tape, 0);

	printf("machine %s hw=%s ram=%dK cpu=%d contio=%d contmem=%d contPattern=%d earlyTiming=%d 4t-border=%d snow=%d fbus=%d psg=%d disk=%d layout=%s\n",
		mac->id, comp->hw->name, mac->ram >> 10, mac->cpufrq, comp->flgCNTI, comp->flgCNTM,
		comp->vid->ula->conttype, comp->vid->ula->early, mac->brd4t, comp->flgSNOW, comp->fbus,
		mac->psgCount, mac->disk, mac->layout);

	if (loadTAP(comp, av[3], 0) != ERR_OK) { fprintf(stderr, "can't load tap %s\n", av[3]); return 1; }

	// autostart.cpp: autostart_arm + autostart_frame
	compUserReset(comp, mac->asRes);
	comp->keyb->scanmask = 0;
	int life = 1500;
	while (comp->keyb->scanmask != 0xff) {
		run_frame(comp);
		if (--life < 0) { fprintf(stderr, "rom never scanned the keyboard\n"); return 1; }
	}
	for (int i = 0; i < 150; i++) run_frame(comp);
	for (int k = 0; mac->asKeys[k]; k++) {
		key(comp, mac->asKeys[k], 1);
		for (int i = 0; i < 4; i++) run_frame(comp);
		key(comp, mac->asKeys[k], 0);
		if (mac->asKeys[k + 1])
			for (int i = 0; i < 8; i++) run_frame(comp);
	}
	tapUserPlay(comp->tape);
	long long playFrame = frames;
	printf("keys typed, tape playing at frame %lld\n", playFrame);

	// clear DONE in case ram came up with a 1 there: the loader writes the code over it anyway
	long long startSeen = -1;
	while (frames < maxfr) {
		run_frame(comp);
		if (startSeen < 0 && (comp->cpu->regPC >= aSTART) && (comp->cpu->regPC < aEND)) {
			startSeen = frames;
			printf("probe code running (PC=#%04X) at frame %lld, IY=#%04X SP=#%04X IM=%d IFF1=%d\n",
				comp->cpu->regPC, frames, comp->cpu->regIY, comp->cpu->regSP, comp->cpu->regIM, comp->cpu->flgIFF1);
		}
		if (startSeen >= 0 && rd(comp, aDONE) == 1) break;
		if ((frames % 5000) == 0) printf("  frame %lld PC=#%04X\n", frames, comp->cpu->regPC);
	}
	int done = rd(comp, aDONE);
	printf("stopped at frame %lld (%lld after Play, %lld after probe start)\n", frames, frames - playFrame,
		startSeen >= 0 ? frames - startSeen : -1);

	char path[4096];
	snprintf(path, sizeof(path), "%s.bin", av[5]);
	FILE* f = fopen(path, "wb");
	if (f) {
		for (int a = aSTART; a < aEND; a++) fputc(rd(comp, a), f);
		fclose(f);
	}
	printf("DONE=%d CLASS=%d ONSET=%d CAPS=#%02X FAILS=%d\n", done, rd(comp, aCLASS),
		rd(comp, aONSET) | (rd(comp, aONSET + 1) << 8), rd(comp, aCAPS),
		rd(comp, aFAILS) | (rd(comp, aFAILS + 1) << 8));
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
	return done == 1 ? 0 : 1;
}
