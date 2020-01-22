#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"

class EmulatorContext;

struct ATA_DEVICE
{
   unsigned c,h,s,lba;
   union
   {
      uint8_t regs[12];
      struct
      {
		  uint8_t data;
		  uint8_t err;             // for write, features
			union
			{
				uint8_t count;
				uint8_t intreason;
			};
		 uint8_t sec;
         union
         {
            uint16_t cyl;
            uint16_t atapi_count;
            struct
            {
				uint8_t cyl_l;
				uint8_t cyl_h;
            };
         };
		 uint8_t devhead;
		 uint8_t status;          // for write, cmd
         /*                  */
		 uint8_t control;         // reg8 - control (CS1,DA=6)
		 uint8_t feat;
		 uint8_t cmd;
		 uint8_t reserved;        // reserved
      } reg;
   };
   uint8_t intrq;
   uint8_t readonly;
   uint8_t device_id;             // 0x00 - master, 0x10 - slave
   uint8_t atapi;                 // flag for CD-ROM device

   uint8_t read(unsigned n_reg);
   void write(unsigned n_reg, uint8_t data);
   unsigned read_data();
   void write_data(unsigned data);
   uint8_t read_intrq();

   char exec_ata_cmd(uint8_t cmd);
   char exec_atapi_cmd(uint8_t cmd);

   enum RESET_TYPE { RESET_HARD, RESET_SOFT, RESET_SRST };
   void reset_signature(RESET_TYPE mode = RESET_SOFT);

   void Reset(RESET_TYPE mode);
   char seek();
   void recalibrate();
   void configure(IDE_CONFIG *cfg);
   void prepare_id();
   void command_ok();
   void next_sector();
   void read_sectors();
   void verify_sectors();
   void write_sectors();
   void format_track();

   enum ATAPI_INT_REASON
   {
      INT_COD       = 0x01,
      INT_IO        = 0x02,
      INT_RELEASE   = 0x04
   };

   enum HD_STATUS
   {
      STATUS_BSY   = 0x80,
      STATUS_DRDY  = 0x40,
      STATUS_DF    = 0x20,
      STATUS_DSC   = 0x10,
      STATUS_DRQ   = 0x08,
      STATUS_CORR  = 0x04,
      STATUS_IDX   = 0x02,
      STATUS_ERR   = 0x01
   };

   enum HD_ERROR
   {
      ERR_BBK   = 0x80,
      ERR_UNC   = 0x40,
      ERR_MC    = 0x20,
      ERR_IDNF  = 0x10,
      ERR_MCR   = 0x08,
      ERR_ABRT  = 0x04,
      ERR_TK0NF = 0x02,
      ERR_AMNF  = 0x01
   };

   enum HD_CONTROL
   {
      CONTROL_SRST = 0x04,
      CONTROL_nIEN = 0x02
   };

   enum HD_STATE
   {
      S_IDLE = 0, S_READ_ID,
      S_READ_SECTORS, S_VERIFY_SECTORS, S_WRITE_SECTORS, S_FORMAT_TRACK,
      S_RECV_PACKET, S_READ_ATAPI,
      S_MODE_SELECT
   };

   HD_STATE state;
   unsigned transptr, transcount;
   unsigned phys_dev;
   uint8_t transbf[0xFFFF]; // ATAPI is able to tranfer 0xFFFF bytes. passing more leads to error

   void handle_atapi_packet();
   void handle_atapi_packet_emulate();
   void exec_mode_select();

   //ATA_PASSER ata_p;
   //ATAPI_PASSER atapi_p;
   //bool loaded() { return ata_p.loaded() || atapi_p.loaded(); } //was crashed at atapi_p.loaded() if no master or slave device!!! see fix in ATAPI_PASSER //Alone Coder
};

struct ATA_PORT
{
   ATA_DEVICE dev[2];
   uint8_t read_high, write_high;

   ATA_PORT() { dev[0].device_id = 0, dev[1].device_id = 0x10; reset(); }

   uint8_t read(unsigned n_reg);
   void write(unsigned n_reg, uint8_t data);
   unsigned read_data();
   void write_data(unsigned data);
   uint8_t read_intrq();

   void reset();
};

//extern PHYS_DEVICE phys[];
//extern int n_phys;

unsigned find_hdd_device(char *name);
void init_hdd_cd();


class HDD
{
protected:
	EmulatorContext* _context = nullptr;

public:
	HDD() = delete;		// Disable default constructor. C++ 11 feature
	HDD(EmulatorContext* context);

	void Reset();
};