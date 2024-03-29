#pragma once
#include "stdafx.h"

class EmulatorContext;

const int Z80FQ = 3500000; // todo: #define as (conf.frame*conf.intfq)
const int FDD_RPS = 5; // rotation speed

const int MAX_TRACK_LEN = 6250;
const int MAX_CYLS = 86;            // don't load images with so many tracks
const int MAX_PHYS_CYL = 86;        // don't seek over it
const int MAX_SEC = 256;

const int ROMLED_TIME = 16;

struct SECHDR
{
   uint8_t c,s,n,l;
   uint16_t crc;
   uint8_t c1, c2; // flags: correct CRCs in address and data
   uint8_t *data, *id;
   unsigned datlen;
   unsigned crcd;        // used to load specific CRC from FDI-file
};

enum SEEK_MODE { JUST_SEEK = 0, LOAD_SECTORS = 1 };

struct TRKCACHE
{
   // cached track position
   struct FDD *drive;
   unsigned cyl, side;

   // generic track data
   unsigned trklen;
   uint8_t *trkd, *trki;       // pointer to data inside UDI
   unsigned ts_byte;                 // cpu.t per byte
   SEEK_MODE sf;                     // flag: is sectors filled
   unsigned s;                       // no. of sectors

   // sectors on track
   SECHDR hdr[MAX_SEC];

   void set_i(unsigned pos) { trki[pos/8] |= 1 << (pos&7); }
   void clr_i(unsigned pos) { trki[pos/8] &= ~(1 << (pos&7)); }
   uint8_t test_i(unsigned pos) { return trki[pos/8] & (1 << (pos&7)); }
   void write(unsigned pos, uint8_t byte, char index)
   {
       if (!trkd)
           return;

       trkd[pos] = byte;
       if (index)
           set_i(pos);
       else
           clr_i(pos);
   }

   void seek(FDD *d, unsigned cyl, unsigned side, SEEK_MODE fs);
   void format(); // before use, call seek(d,c,s,JUST_SEEK), set s and hdr[]
   int write_sector(unsigned sec, uint8_t *data); // call seek(d,c,s,LOAD_SECTORS)
   const SECHDR *get_sector(unsigned sec) const; // before use, call fill(d,c,s,LOAD_SECTORS)

   void dump();
   void clear()
   {
       drive = 0;
       trkd = 0;
       ts_byte = Z80FQ/(MAX_TRACK_LEN * FDD_RPS);
   }
   TRKCACHE() { clear(); }
};


struct FDD
{
	uint8_t Id;
   // drive data

   int64_t motor;       // 0 - not spinning, >0 - time when it'll stop
   uint8_t track; // head position

   // disk data

   uint8_t *rawdata;              // used in VirtualAlloc/VirtualFree
   unsigned rawsize;
   unsigned cyls, sides;
   unsigned trklen[MAX_CYLS][2];
   uint8_t *trkd[MAX_CYLS][2];
   uint8_t *trki[MAX_CYLS][2];
   uint8_t optype; // bits: 0-not modified, 1-write sector, 2-format track
   uint8_t snaptype;

   TRKCACHE t; // used in read/write image
   char name[0x200];
   char dsc[0x200];

   char test();
   void free();
   int index();

   void format_trd();
   void emptydisk();
   void newdisk(unsigned cyls, unsigned sides);
   int addfile(uint8_t *hdr, uint8_t *data);
   void addboot();

   int read(uint8_t snType);

   int read_scl();
   int read_hob();
   int read_trd();
   int write_trd(FILE *ff);
   int read_fdi();
   int write_fdi(FILE *ff);
   int read_td0();
   int write_td0(FILE *ff);
   int read_udi();
   int write_udi(FILE *ff);

   void format_isd();
   int read_isd();
   int write_isd(FILE *ff);

   void format_pro();
   int read_pro();
   int write_pro(FILE *ff);

   ~FDD() { free(); }
};


struct WD1793
{
   enum WDSTATE
   {
      S_IDLE = 0,
      S_WAIT,

      S_DELAY_BEFORE_CMD,
      S_CMD_RW,
      S_FOUND_NEXT_ID,
      S_RDSEC,
      S_READ,
      S_WRSEC,
      S_WRITE,
      S_WRTRACK,
      S_WR_TRACK_DATA,

      S_TYPE1_CMD,
      S_STEP,
      S_SEEKSTART,
      S_SEEK,
      S_VERIFY,

      S_RESET
   };

protected:
	EmulatorContext* _context = nullptr;

public:
   int64_t next, time;
   int64_t idx_tmo;

   FDD *seldrive;
   unsigned tshift;

   WDSTATE wd_state, wd_state2;

   uint8_t cmd;
   uint8_t data, track, sector;
   uint8_t rqs, status;
   uint8_t idx_status;

   unsigned drive, side;                // update this with changing 'system'

   char stepdirection;
   uint8_t system;                // beta128 system register

   unsigned idx_cnt; // idx counter

   // read/write sector(s) data
   int64_t end_waiting_am;
   unsigned foundid;                    // index in trkcache.hdr for next encountered ID and bytes before this ID
   unsigned rwptr, rwlen;

   // format track data
   unsigned start_crc;

   uint8_t trdos_load, trdos_save, trdos_format, trdos_seek; // for leds

   enum CMDBITS
   {
      CMD_SEEK_RATE     = 0x03,
      CMD_SEEK_VERIFY   = 0x04,
      CMD_SEEK_HEADLOAD = 0x08,
      CMD_SEEK_TRKUPD   = 0x10,
      CMD_SEEK_DIR      = 0x20,

      CMD_WRITE_DEL     = 0x01,
      CMD_SIDE_CMP_FLAG = 0x02,
      CMD_DELAY         = 0x04,
      CMD_SIDE          = 0x08,
      CMD_SIDE_SHIFT    = 3,
      CMD_MULTIPLE      = 0x10
   };

   enum BETA_STATUS
   {
      DRQ   = 0x40,
      INTRQ = 0x80
   };

   enum WD_STATUS
   {
      WDS_BUSY      = 0x01,
      WDS_INDEX     = 0x02,
      WDS_DRQ       = 0x02,
      WDS_TRK00     = 0x04,
      WDS_LOST      = 0x04,
      WDS_CRCERR    = 0x08,
      WDS_NOTFOUND  = 0x10,
      WDS_SEEKERR   = 0x10,
      WDS_RECORDT   = 0x20,
      WDS_HEADL     = 0x20,
      WDS_WRFAULT   = 0x20,
      WDS_WRITEP    = 0x40,
      WDS_NOTRDY    = 0x80
   };

   enum WD_SYS
   {
      SYS_HLT       = 0x08
   };

   uint8_t in(uint8_t port);
   void out(uint8_t port, uint8_t val);

   void process();
   void find_marker();
   char notready();
   void load();
   void getindex();
   void trdos_traps();

//   TRKCACHE trkcache;
   FDD fdd[4];

   WD1793()
   {
       for (unsigned i = 0; i < 4; i++) // [vv] ��� �������� �������
           fdd[i].Id = i;
       seldrive = &fdd[0];
       idx_cnt = 0;
       idx_status = 0;
       idx_tmo = LLONG_MAX;
   }
};
