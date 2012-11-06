/***************************************************************************
    commodore c64 home computer

    peter.trauner@jk.uni-linz.ac.at
    documentation
     www.funet.fi
***************************************************************************/

/*
    2008-09-06: Tape status for C64 & C128 [FP & RZ]
    - tape loading works
    - tap files are supported
    - tape writing works
*/

#include "emu.h"

#include "cpu/m6502/m6510.h"
#include "cpu/z80/z80.h"
#include "sound/sid6581.h"
#include "machine/6526cia.h"
#include "machine/cbmiec.h"
#include "video/vic6567.h"

#include "includes/cbm.h"
#include "includes/c64_legacy.h"

#include "imagedev/cassette.h"
#include "imagedev/cartslot.h"

#define VERBOSE_LEVEL 0
#define DBG_LOG( MACHINE, N, M, A ) \
	do { \
		if(VERBOSE_LEVEL >= N) \
		{ \
			if( M ) \
				logerror("%11.6f: %-24s", MACHINE.time().as_double(), (char*) M ); \
			logerror A; \
		} \
	} while (0)

#define log_cart 0

/* Info from http://unusedino.de/ec64/technical/misc/c64/64doc.html */
/*

  The leftmost column of the table contains addresses in hexadecimal
notation. The columns aside it introduce all possible memory
configurations. The default mode is on the left, and the absolutely
most rarely used Ultimax game console configuration is on the right.
(Has anybody ever seen any Ultimax games?) Each memory configuration
column has one or more four-digit binary numbers as a title. The bits,
from left to right, represent the state of the -LORAM, -HIRAM, -GAME
and -EXROM lines, respectively. The bits whose state does not matter
are marked with "x". For instance, when the Ultimax video game
configuration is active (the -GAME line is shorted to ground), the
-LORAM and -HIRAM lines have no effect.

      default                      001x                       Ultimax
       1111   101x   1000   011x   00x0   1110   0100   1100   xx01
10000
----------------------------------------------------------------------
 F000
       Kernal RAM    RAM    Kernal RAM    Kernal Kernal Kernal ROMH(*
 E000
----------------------------------------------------------------------
 D000  IO/C   IO/C   IO/RAM IO/C   RAM    IO/C   IO/C   IO/C   I/O
----------------------------------------------------------------------
 C000  RAM    RAM    RAM    RAM    RAM    RAM    RAM    RAM     -
----------------------------------------------------------------------
 B000
       BASIC  RAM    RAM    RAM    RAM    BASIC  ROMH   ROMH    -
 A000
----------------------------------------------------------------------
 9000
       RAM    RAM    RAM    RAM    RAM    ROML   RAM    ROML   ROML(*
 8000
----------------------------------------------------------------------
 7000

 6000
       RAM    RAM    RAM    RAM    RAM    RAM    RAM    RAM     -
 5000

 4000
----------------------------------------------------------------------
 3000

 2000  RAM    RAM    RAM    RAM    RAM    RAM    RAM    RAM     -

 1000
----------------------------------------------------------------------
 0000  RAM    RAM    RAM    RAM    RAM    RAM    RAM    RAM    RAM
----------------------------------------------------------------------

   *) Internal memory does not respond to write accesses to these
       areas.


    Legend: Kernal      E000-FFFF       Kernal ROM.

            IO/C        D000-DFFF       I/O address space or Character
                                        generator ROM, selected by
                                        -CHAREN. If the CHAREN bit is
                                        clear, the character generator
                                        ROM will be selected. If it is
                                        set, the I/O chips are
                                        accessible.

            IO/RAM      D000-DFFF       I/O address space or RAM,
                                        selected by -CHAREN. If the
                                        CHAREN bit is clear, the
                                        character generator ROM will
                                        be selected. If it is set, the
                                        internal RAM is accessible.

            I/O         D000-DFFF       I/O address space.
                                        The -CHAREN line has no effect.

            BASIC       A000-BFFF       BASIC ROM.

            ROMH        A000-BFFF or    External ROM with the -ROMH line
                        E000-FFFF       connected to its -CS line.

            ROML        8000-9FFF       External ROM with the -ROML line
                                        connected to its -CS line.

            RAM         various ranges  Commodore 64's internal RAM.

            -           1000-7FFF and   Open address space.
                        A000-CFFF       The Commodore 64's memory chips
                                        do not detect any memory accesses
                                        to this area except the VIC-II's
                                        DMA and memory refreshes.

    NOTE:   Whenever the processor tries to write to any ROM area
            (Kernal, BASIC, CHAROM, ROML, ROMH), the data will get
            "through the ROM" to the C64's internal RAM.

            For this reason, you can easily copy data from ROM to RAM,
            without any bank switching. But implementing external
            memory expansions without DMA is very hard, as you have to
            use a 256 byte window on the I/O1 or I/O2 area, like
            GEORAM, or the Ultimax memory configuration, if you do not
            want the data to be written both to internal and external
            RAM.

            However, this is not true for the Ultimax video game
            configuration. In that mode, the internal RAM ignores all
            memory accesses outside the area $0000-$0FFF, unless they
            are performed by the VIC, and you can write to external
            memory at $1000-$CFFF and $E000-$FFFF, if any, without
            changing the contents of the internal RAM.

*/

static void c64_bankswitch( running_machine &machine, int reset )
{
	legacy_c64_state *state = machine.driver_data<legacy_c64_state>();
	int loram, hiram, charen;
	int ultimax_mode = 0;
	int data = machine.device<m6510_device>("maincpu")->get_port() & 0x07;

	/* Are we in Ultimax mode? */
	if (!state->m_game && state->m_exrom)
		ultimax_mode = 1;

	DBG_LOG(machine, 1, "bankswitch", ("%d\n", data & 7));
	loram  = (data & 1) ? 1 : 0;
	hiram  = (data & 2) ? 1 : 0;
	charen = (data & 4) ? 1 : 0;
	//logerror("Bankswitch mode || charen, state->m_ultimax\n");
	//logerror("%d, %d, %d, %d  ||   %d,      %d  \n", loram, hiram, state->m_game, state->m_exrom, charen, ultimax_mode);

	if (ultimax_mode)
	{
			state->m_io_enabled = 1;		// charen has no effect in ultimax_mode

			state->membank("bank1")->set_base(state->m_roml);
			state->membank("bank3")->set_base(state->m_memory + 0xa000);
			state->membank("bank4")->set_base(state->m_romh);
			machine.device("maincpu")->memory().space(AS_PROGRAM).nop_write(0xe000, 0xffff);
	}
	else
	{
		/* 0x8000-0x9000 */
		if (loram && hiram && !state->m_exrom)
		{
			state->membank("bank1")->set_base(state->m_roml);
		}
		else
		{
			state->membank("bank1")->set_base(state->m_memory + 0x8000);
		}

		/* 0xa000 */
		if (hiram && !state->m_game && !state->m_exrom)
			state->membank("bank3")->set_base(state->m_romh);

		else if (loram && hiram && state->m_game)
			state->membank("bank3")->set_base(state->m_basic);

		else
			state->membank("bank3")->set_base(state->m_memory + 0xa000);

		/* 0xd000 */
		// RAM
		if (!loram && !hiram && (state->m_game || !state->m_exrom))
		{
			state->m_io_enabled = 0;
			state->m_io_ram_r_ptr = state->m_memory + 0xd000;
			state->m_io_ram_w_ptr = state->m_memory + 0xd000;
		}
		// IO/RAM
		else if (loram && !hiram && !state->m_game)	// remember we cannot be in ultimax_mode, no need of !state->m_exrom
		{
			state->m_io_enabled = 1;
			state->m_io_ram_r_ptr = (!charen) ? state->m_chargen : state->m_memory + 0xd000;
			state->m_io_ram_w_ptr = state->m_memory + 0xd000;
		}
		// IO/C
		else
		{
			state->m_io_enabled = charen ? 1 : 0;

			if (!charen)
			{
			state->m_io_ram_r_ptr = state->m_chargen;
			state->m_io_ram_w_ptr = state->m_memory + 0xd000;
			}
		}

		/* 0xe000-0xf000 */
		state->membank("bank4")->set_base(hiram ? state->m_kernal : state->m_memory + 0xe000);
		state->membank("bank5")->set_base(state->m_memory + 0xe000);
	}

	/* make sure the opbase function gets called each time */
	/* NPW 15-May-2008 - Another hack in the C64 drivers broken! */
	/* opbase->mem_max = 0xcfff; */

	state->m_old_game = state->m_game;
	state->m_old_exrom = state->m_exrom;
	state->m_old_data = data;
}

int c64_paddle_read( device_t *device, address_space &space, int which )
{
	running_machine &machine = device->machine();
	int pot1 = 0xff, pot2 = 0xff, pot3 = 0xff, pot4 = 0xff, temp;
	UINT8 cia0porta = mos6526_pa_r(machine.device("cia_0"), space, 0);
	int controller1 = machine.root_device().ioport("CTRLSEL")->read() & 0x07;
	int controller2 = machine.root_device().ioport("CTRLSEL")->read() & 0x70;
	/* Notice that only a single input is defined for Mouse & Lightpen in both ports */
	switch (controller1)
	{
		case 0x01:
			if (which)
				pot2 = machine.root_device().ioport("PADDLE2")->read();
			else
				pot1 = machine.root_device().ioport("PADDLE1")->read();
			break;

		case 0x02:
			if (which)
				pot2 = machine.root_device().ioport("TRACKY")->read();
			else
				pot1 = machine.root_device().ioport("TRACKX")->read();
			break;

		case 0x03:
			if (which && (machine.root_device().ioport("JOY1_2B")->read() & 0x20))	/* Joy1 Button 2 */
				pot1 = 0x00;
			break;

		case 0x04:
			if (which)
				pot2 = machine.root_device().ioport("LIGHTY")->read();
			else
				pot1 = machine.root_device().ioport("LIGHTX")->read();
			break;

		case 0x06:
			if (which && (machine.root_device().ioport("OTHER")->read() & 0x04))	/* Lightpen Signal */
				pot2 = 0x00;
			break;

		case 0x00:
		case 0x07:
			break;

		default:
			logerror("Invalid Controller Setting %d\n", controller1);
			break;
	}

	switch (controller2)
	{
		case 0x10:
			if (which)
				pot4 = machine.root_device().ioport("PADDLE4")->read();
			else
				pot3 = machine.root_device().ioport("PADDLE3")->read();
			break;

		case 0x20:
			if (which)
				pot4 = machine.root_device().ioport("TRACKY")->read();
			else
				pot3 = machine.root_device().ioport("TRACKX")->read();
			break;

		case 0x30:
			if (which && (machine.root_device().ioport("JOY2_2B")->read() & 0x20))	/* Joy2 Button 2 */
				pot4 = 0x00;
			break;

		case 0x40:
			if (which)
				pot4 = machine.root_device().ioport("LIGHTY")->read();
			else
				pot3 = machine.root_device().ioport("LIGHTX")->read();
			break;

		case 0x60:
			if (which && (machine.root_device().ioport("OTHER")->read() & 0x04))	/* Lightpen Signal */
				pot4 = 0x00;
			break;

		case 0x00:
		case 0x70:
			break;

		default:
			logerror("Invalid Controller Setting %d\n", controller1);
			break;
	}

	if (machine.root_device().ioport("CTRLSEL")->read() & 0x80)		/* Swap */
	{
		temp = pot1; pot1 = pot3; pot3 = temp;
		temp = pot2; pot2 = pot4; pot4 = temp;
	}

	switch (cia0porta & 0xc0)
	{
		case 0x40:
			return which ? pot2 : pot1;

		case 0x80:
			return which ? pot4 : pot3;

		case 0xc0:
			return which ? pot2 : pot1;

		default:
			return 0;
	}
}

READ8_HANDLER( c64_colorram_read )
{
	legacy_c64_state *state = space.machine().driver_data<legacy_c64_state>();
	return state->m_colorram[offset & 0x3ff];
}

WRITE8_HANDLER( c64_colorram_write )
{
	legacy_c64_state *state = space.machine().driver_data<legacy_c64_state>();
	state->m_colorram[offset & 0x3ff] = data | 0xf0;
}

/***********************************************

    C64 Cartridges

***********************************************/

/* Info based on http://ist.uwaterloo.ca/~schepers/formats/CRT.TXT      */
/* Please refer to the webpage for the latest version and for a very
   complete listing of various cart types and their bankswitch tricks   */
/*
  Cartridge files were introduced in the CCS64  emulator,  written  by  Per
Hakan Sundell, and use the ".CRT" file extension. This format  was  created
to handle the various ROM cartridges that exist, such as Action Replay, the
Power cartridge, and the Final Cartridge.

  Normal game cartridges can load  into  several  different  memory  ranges
($8000-9FFF,  $A000-BFFF  or  $E000-FFFF).  Newer   utility   and   freezer
cartridges were less intrusive, hiding themselves until  called  upon,  and
still others used bank-switching techniques to allow much larger ROM's than
normal. Because of these "stealthing" and bank-switching methods, a special
cartridge format  was  necessary,  to  let  the  emulator  know  where  the
cartridge should reside, the control line  states  to  enable  it  and  any
special hardware features it uses.

(...)

[ A .CRT file consists of

    $0000-0040 :    Header of the whole .crt files
    $0040-EOF :     Blocks of data

  Each block of data, called 'CHIP', can be of variable size. The first
0x10 bytes of each CHIP block is the block header, and it contains various
informations on the block itself, as its size (both with and without the
header), the loading address and an index to identify which memory bank
the data must be loaded to.  FP ]

.CRT header description
-----------------------

 Bytes: $0000-000F - 16-byte cartridge signature  "C64  CARTRIDGE"  (padded
                     with space characters)
         0010-0013 - File header length  ($00000040,  in  high/low  format,
                     calculated from offset $0000). The default  (also  the
                     minimum) value is $40.  Some  cartridges  exist  which
                     show a value of $00000020 which is wrong.
         0014-0015 - Cartridge version (high/low, presently 01.00)
         0016-0017 - Cartridge hardware type ($0000, high/low)
              0018 - Cartridge port EXROM line status
                      0 - inactive
                      1 - active
              0019 - Cartridge port GAME line status
                      0 - inactive
                      1 - active
         001A-001F - Reserved for future use
         0020-003F - 32-byte cartridge  name  "CCSMON"  (uppercase,  padded
                     with null characters)
         0040-xxxx - Cartridge contents (called CHIP PACKETS, as there  can
                     be more than one  per  CRT  file).  See  below  for  a
                     breakdown of the CHIP format.

CHIP content description
------------------------

[ Addresses shifted back to $0000.  FP ]

 Bytes: $0000-0003 - Contained ROM signature "CHIP" (note there can be more
                     than one image in a .CRT file)
         0004-0007 - Total packet length (ROM  image  size  and
                     header combined) (high/low format)
         0008-0009 - Chip type
                      0 - ROM
                      1 - RAM, no ROM data
                      2 - Flash ROM
         000A-000B - Bank number
         000C-000D - Starting load address (high/low format)
         000E-000F - ROM image size in bytes  (high/low  format,  typically
                     $2000 or $4000)
         0010-xxxx - ROM data


*/


/* Hardware Types for C64 carts */
enum {
	GENERIC_CRT = 0,		/* 00 - Normal cartridge                    */
	ACTION_REPLAY,		/* 01 - Action Replay                       */
	KCS_PC,			/* 02 - KCS Power Cartridge                 */
	FINAL_CART_III,		/* 03 - Final Cartridge III                 */
	SIMONS_BASIC,		/* 04 - Simons Basic                        */
	OCEAN_1,			/* 05 - Ocean type 1 (1)                    */
	EXPERT,			/* 06 - Expert Cartridge                    */
	FUN_PLAY,			/* 07 - Fun Play, Power Play                */
	SUPER_GAMES,		/* 08 - Super Games                         */
	ATOMIC_POWER,		/* 09 - Atomic Power                        */
	EPYX_FASTLOAD,		/* 10 - Epyx Fastload                       */
	WESTERMANN,			/* 11 - Westermann Learning                 */
	REX,				/* 12 - Rex Utility                         */
	FINAL_CART_I,		/* 13 - Final Cartridge I                   */
	MAGIC_FORMEL,		/* 14 - Magic Formel                        */
	C64GS,			/* 15 - C64 Game System, System 3           */
	WARPSPEED,			/* 16 - WarpSpeed                           */
	DINAMIC,			/* 17 - Dinamic (2)                         */
	ZAXXON,			/* 18 - Zaxxon, Super Zaxxon (SEGA)         */
	DOMARK,			/* 19 - Magic Desk, Domark, HES Australia   */
	SUPER_SNAP_5,		/* 20 - Super Snapshot 5                    */
	COMAL_80,			/* 21 - Comal-80                            */
	STRUCT_BASIC,		/* 22 - Structured Basic                    */
	ROSS,				/* 23 - Ross                                */
	DELA_EP64,			/* 24 - Dela EP64                           */
	DELA_EP7X8,			/* 25 - Dela EP7x8                          */
	DELA_EP256,			/* 26 - Dela EP256                          */
	REX_EP256,			/* 27 - Rex EP256                           */
	MIKRO_ASSMBLR,		/* 28 - Mikro Assembler                     */
	REAL_FC_I,			/* 29 - (3)                                 */
	ACTION_REPLAY_4,		/* 30 - Action Replay 4                     */
	STARDOS,			/* 31 - StarDOS                             */
	/*
    (1) Ocean type 1 includes Navy Seals, Robocop 2 & 3,  Shadow  of
    the Beast, Toki, Terminator 2 and more. Both 256 and 128 Kb images.
    (2) Dinamic includes Narco Police and more.
    (3) Type 29 is reserved for the real Final Cartridge I, the one
    above (Type 13) will become Final Cartridge II.                 */
	/****************************************
    Vice also defines the following types:
    #define CARTRIDGE_ACTION_REPLAY3    -29
    #define CARTRIDGE_IEEE488           -11
    #define CARTRIDGE_IDE64             -7
    #define CARTRIDGE_RETRO_REPLAY      -5
    #define CARTRIDGE_SUPER_SNAPSHOT    -4

    Can we support these as well?
    *****************************************/
};

static DEVICE_IMAGE_UNLOAD( c64_cart )
{
	legacy_c64_state *state = image.device().machine().driver_data<legacy_c64_state>();
	int i;

	for (i = 0; i < C64_MAX_ROMBANK; i++)
	{
		state->m_cart.bank[i].size = 0;
		state->m_cart.bank[i].addr = 0;
		state->m_cart.bank[i].index = 0;
		state->m_cart.bank[i].start = 0;
	}
}


static DEVICE_START( c64_cart )
{
	legacy_c64_state *state = device->machine().driver_data<legacy_c64_state>();
	/* In the first slot we can load a .crt file. In this case we want
        to use game & exrom values from the header, not the default ones. */
	state->m_cart.game = -1;
	state->m_cart.exrom = -1;
	state->m_cart.mapper = GENERIC_CRT;
	state->m_cart.n_banks = 0;
}

static int c64_crt_load( device_image_interface &image )
{
	legacy_c64_state *state = image.device().machine().driver_data<legacy_c64_state>();
	int size = image.length(), test, i = 0, ii;
	int _80_loaded = 0, _90_loaded = 0, a0_loaded = 0, b0_loaded = 0, e0_loaded = 0, f0_loaded = 0;
	const char *filetype = image.filetype();
	int address = 0, new_start = 0;
	// int lbank_end_addr = 0, hbank_end_addr = 0;
	UINT8 *cart_cpy = state->memregion("user1")->base();

	/* We support .crt files */
	if (!mame_stricmp(filetype, "crt"))
	{
		int j;
		unsigned short c64_cart_type;

		if (i >= C64_MAX_ROMBANK)
			return IMAGE_INIT_FAIL;

		/* Start to parse the .crt header */
		/* 0x16-0x17 is Hardware type */
		image.fseek(0x16, SEEK_SET);
		image.fread(&c64_cart_type, 2);
		state->m_cart.mapper = BIG_ENDIANIZE_INT16(c64_cart_type);

		/* If it is unsupported cart type, warn the user */
		switch (state->m_cart.mapper)
		{
			case SIMONS_BASIC:	/* Type #  4 */
			case OCEAN_1:		/* Type #  5 */
			case FUN_PLAY:		/* Type #  7 */
			case SUPER_GAMES:		/* Type #  8 */
			case EPYX_FASTLOAD:	/* Type # 10 */
			case REX:			/* Type # 12 */
			case C64GS:			/* Type # 15 */
			case DINAMIC:		/* Type # 17 */
			case ZAXXON:		/* Type # 18 */
			case DOMARK:		/* Type # 19 */
			case COMAL_80:		/* Type # 21 */
			case GENERIC_CRT:		/* Type #  0 */
				printf("Currently supported cart type (Type %d)\n", state->m_cart.mapper);
				break;

			default:
			case ACTION_REPLAY:	/* Type #  1 */
			case KCS_PC:		/* Type #  2 */
			case FINAL_CART_III:	/* Type #  3 */
			case EXPERT:		/* Type #  6 */
			case ATOMIC_POWER:	/* Type #  9 */
			case WESTERMANN:		/* Type # 11 */
			case FINAL_CART_I:	/* Type # 13 */
			case MAGIC_FORMEL:	/* Type # 14 */
			case SUPER_SNAP_5:	/* Type # 20 */
				printf("Currently unsupported cart type (Type %d)\n", state->m_cart.mapper);
				break;
		}

		/* 0x18 is EXROM */
		image.fseek(0x18, SEEK_SET);
		image.fread(&state->m_cart.exrom, 1);

		/* 0x19 is GAME */
		image.fread(&state->m_cart.game, 1);

		/* We can pass to the data: it starts from 0x40 */
		image.fseek(0x40, SEEK_SET);
		j = 0x40;

		logerror("Loading cart %s size:%.4x\n", image.filename(), size);
		logerror("Header info: EXROM %d, GAME %d, Cart Type %d \n", state->m_cart.exrom, state->m_cart.game, c64_cart_type);


		/* Data in a .crt image are organized in blocks called 'CHIP':
           each 'CHIP' consists of a 0x10 header, which contains the
           actual size of the block, the loading address and info on
           the bankswitch, followed by the actual data                  */
		while (j < size)
		{
			unsigned short chip_size, chip_bank_index, chip_data_size;
			unsigned char buffer[10];

			/* Start to parse the CHIP header */
			/* First 4 bytes are the string 'CHIP' */
			image.fread(buffer, 6);

			/* 0x06-0x07 is the size of the CHIP block (header + data) */
			image.fread(&chip_size, 2);
			chip_size = BIG_ENDIANIZE_INT16(chip_size);

			/* 0x08-0x09 chip type (ROM, RAM + no ROM, Flash ROM) */
			image.fread(buffer + 6, 2);

			/* 0x0a-0x0b is the bank number of the CHIP block */
			image.fread(&chip_bank_index, 2);
			chip_bank_index = BIG_ENDIANIZE_INT16(chip_bank_index);

			/* 0x0c-0x0d is the loading address of the CHIP block */
			image.fread(&address, 2);
			address = BIG_ENDIANIZE_INT16(address);

			/* 0x0e-0x0f is the data size of the CHIP block (without header) */
			image.fread(&chip_data_size, 2);
			chip_data_size = BIG_ENDIANIZE_INT16(chip_data_size);

			/* Print out the CHIP header! */
			logerror("%.4s %.2x %.2x %.4x %.2x %.2x %.4x %.4x:%.4x\n",
				buffer, buffer[4], buffer[5], chip_size,
				buffer[6], buffer[7], chip_bank_index,
				address, chip_data_size);
			logerror("Loading CHIP data at %.4x size:%.4x\n", address, chip_data_size);

			/* Store data, address & size of the CHIP block */
			state->m_cart.bank[i].addr = address;
			state->m_cart.bank[i].index = chip_bank_index;
			state->m_cart.bank[i].size = chip_data_size;
			state->m_cart.bank[i].start = new_start;

			test = image.fread(cart_cpy + new_start, state->m_cart.bank[i].size);
			new_start += state->m_cart.bank[i].size;

			/* Does CHIP contain any data? */
			if (test != state->m_cart.bank[i].size)
				return IMAGE_INIT_FAIL;

			/* Advance to the next CHIP block */
			i++;
			j += chip_size;
		}
	}
	else /* We also support .80 files for c64 & .e0/.f0 for max */
	{
		/* Assign loading address according to extension */
		if (!mame_stricmp(filetype, "80"))
			address = 0x8000;

		if (!mame_stricmp(filetype, "e0"))
			address = 0xe000;

		if (!mame_stricmp(filetype, "f0"))
			address = 0xf000;

		logerror("loading %s rom at %.4x size:%.4x\n", image.filename(), address, size);

		/* Store data, address & size */
		state->m_cart.bank[0].addr = address;
		state->m_cart.bank[0].size = size;
		state->m_cart.bank[0].start = new_start;

		test = image.fread(cart_cpy + new_start, state->m_cart.bank[0].size);
		new_start += state->m_cart.bank[0].size;

		/* Does cart contain any data? */
		if (test != state->m_cart.bank[0].size)
			return IMAGE_INIT_FAIL;
	}

	state->m_cart.n_banks = i; // this is also needed so that we only set mappers if a cart is present!

	/* If we load a .crt file, use EXROM & GAME from the header! */
	if ((state->m_cart.exrom != -1) && (state->m_cart.game != -1))
	{
		state->m_exrom = state->m_cart.exrom;
		state->m_game  = state->m_cart.game;
	}

	/* Finally load the cart */
	state->m_roml = state->m_c64_roml;
	state->m_romh = state->m_c64_romh;

	memset(state->m_roml, 0, 0x2000);
	memset(state->m_romh, 0, 0x2000);

	switch (state->m_cart.mapper)
	{
	default:
		if (!state->m_game && state->m_exrom && (state->m_cart.n_banks == 1))
		{
			memcpy(state->m_romh, cart_cpy, 0x2000);
		}
		else
		{
			// we first attempt to load the first 'CHIPs' with address 0x8000-0xb000 and 0xe000-0xf000, otherwise we load the first (or first two) 'CHIPs' of the image
			for (ii = 0; ii < state->m_cart.n_banks; ii++)
			{
				if (state->m_cart.bank[ii].addr == 0x8000 && !_80_loaded)
				{
					memcpy(state->m_roml, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					_80_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x1000)
						_90_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x2000)
						a0_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x3000)
						b0_loaded = 1;
//                  printf("addr 0x8000: 80 %d, 90 %d, a0 %d, b0 %d\n", _80_loaded, _90_loaded, a0_loaded, b0_loaded);
				}

				if (state->m_cart.bank[ii].addr == 0x9000 && !_90_loaded)
				{
					memcpy(state->m_roml + 0x1000, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					_90_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x1000)
						a0_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x2000)
						b0_loaded = 1;
//                  printf("addr 0x9000: 80 %d, 90 %d, a0 %d, b0 %d\n", _80_loaded, _90_loaded, a0_loaded, b0_loaded);
				}

				if (state->m_cart.bank[ii].addr == 0xa000 && !a0_loaded)
				{
					memcpy(state->m_roml + 0x2000, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					a0_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x1000)
						b0_loaded = 1;
//                  printf("addr 0xa000: 80 %d, 90 %d, a0 %d, b0 %d\n", _80_loaded, _90_loaded, a0_loaded, b0_loaded);
				}

				if (state->m_cart.bank[ii].addr == 0xb000 && !b0_loaded)
				{
					memcpy(state->m_roml + 0x3000, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					b0_loaded = 1;
//                  printf("addr 0xb000: 80 %d, 90 %d, a0 %d, b0 %d\n", _80_loaded, _90_loaded, a0_loaded, b0_loaded);
				}

				if (state->m_cart.bank[ii].addr == 0xe000 && !e0_loaded)
				{
					memcpy(state->m_romh, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					e0_loaded = 1;
					if (state->m_cart.bank[ii].size > 0x1000)
						f0_loaded = 1;
//                  printf("addr 0xe000: e0 %d, f0 %d\n", e0_loaded, f0_loaded);
				}

				if (state->m_cart.bank[ii].addr == 0xf000 && !f0_loaded)
				{
					memcpy(state->m_romh + 0x1000, cart_cpy + state->m_cart.bank[ii].start, state->m_cart.bank[ii].size);
					f0_loaded = 1;
//                  printf("addr 0xe000: e0 %d, f0 %d\n", e0_loaded, f0_loaded);
				}
			}
		}
	}

	return IMAGE_INIT_PASS;
}

/***************************************************************************
    SOFTWARE LIST CARTRIDGE HANDLING
***************************************************************************/

#define install_write_handler(_start, _end, _handler) \
	image.device().machine().firstcpu->space(AS_PROGRAM).install_legacy_write_handler(_start, _end, FUNC(_handler));

#define install_io1_handler(_handler) \
	image.device().machine().firstcpu->space(AS_PROGRAM).install_legacy_write_handler(0xde00, 0xde00, 0, 0xff, FUNC(_handler));

#define install_io2_handler(_handler) \
	image.device().machine().firstcpu->space(AS_PROGRAM).install_legacy_write_handler(0xdf00, 0xdf00, 0, 0xff, FUNC(_handler));

#define allocate_cartridge_timer(_period, _func) \
	legacy_c64_state *state = image.device().machine().driver_data<legacy_c64_state>(); \
	state->m_cartridge_timer = image.device().machine().scheduler().timer_alloc(FUNC(_func)); \
	state->m_cartridge_timer->adjust(_period, 0);

#define set_game_line(_machine, _state) \
	_machine.driver_data<legacy_c64_state>()->m_game = _state; \
	c64_bankswitch(_machine, 0);

INLINE void load_cartridge_region(device_image_interface &image, const char *name, offs_t offset, size_t size)
{
	UINT8 *cart = image.device().machine().root_device().memregion("user1")->base();
	UINT8 *rom = image.get_software_region(name);
	memcpy(cart + offset, rom, size);
}

INLINE void map_cartridge_roml(running_machine &machine, offs_t offset)
{
	legacy_c64_state *state = machine.driver_data<legacy_c64_state>();
	UINT8 *cart = state->memregion("user1")->base();
	memcpy(state->m_roml, cart + offset, 0x2000);
}

INLINE void map_cartridge_romh(running_machine &machine, offs_t offset)
{
	legacy_c64_state *state = machine.driver_data<legacy_c64_state>();
	UINT8 *cart = state->memregion("user1")->base();
	memcpy(state->m_romh, cart + offset, 0x2000);
}

static void load_standard_c64_cartridge(device_image_interface &image)
{
	legacy_c64_state *state = image.device().machine().driver_data<legacy_c64_state>();
	UINT32 size;

	// is there anything to load at 0x8000?
	size = image.get_software_region_length("roml");

	if (size)
	{
		memcpy(state->m_roml, image.get_software_region("roml"), MIN(0x2000, size));

		if (size == 0x4000)
		{
			// continue loading to ROMH region
			memcpy(state->m_romh, image.get_software_region("roml") + 0x2000, 0x2000);
		}
	}

	// is there anything to load at 0xa000?
	size = image.get_software_region_length("romh");
	if (size)
		memcpy(state->m_romh, image.get_software_region("romh"), size);
}

static TIMER_CALLBACK( vizawrite_timer )
{
	map_cartridge_roml(machine, 0x2000);
	set_game_line(machine, 1);
}

static void load_vizawrite_cartridge(device_image_interface &image)
{
	#define VW64_DECRYPT_ADDRESS(_offset) \
		BITSWAP16(_offset,15,14,13,12,7,8,6,9,5,11,4,3,2,10,1,0)

	#define VW64_DECRYPT_DATA(_data) \
		BITSWAP8(_data,7,6,0,5,1,4,2,3)

	UINT8 *roml = image.get_software_region("roml");
	UINT8 *romh = image.get_software_region("romh");
	UINT8 *decrypted = image.device().machine().root_device().memregion("user1")->base();

	// decrypt ROMs
	for (offs_t offset = 0; offset < 0x2000; offset++)
	{
		offs_t address = VW64_DECRYPT_ADDRESS(offset);
		decrypted[address] = VW64_DECRYPT_DATA(roml[offset]);
		decrypted[address + 0x2000] = VW64_DECRYPT_DATA(roml[offset + 0x2000]);
		decrypted[address + 0x4000] = VW64_DECRYPT_DATA(romh[offset]);
	}

	// map cartridge ROMs
	map_cartridge_roml(image.device().machine(), 0x0000);
	map_cartridge_romh(image.device().machine(), 0x4000);

	// allocate GAME changing timer
	allocate_cartridge_timer(attotime::from_msec(1184), vizawrite_timer);
}

static WRITE8_HANDLER( hugo_bank_w )
{
	/*

        bit     description

        0
        1
        2
        3
        4       A14
        5       A15
        6       A16
        7       A13

    */

	int bank = ((data >> 3) & 0x0e) | BIT(data, 7);

	map_cartridge_roml(space.machine(), bank * 0x2000);
}

static void load_hugo_cartridge(device_image_interface &image)
{
	#define HUGO_DECRYPT_ADDRESS(_offset) \
		BITSWAP16(_offset,15,14,13,12,7,6,5,4,3,2,1,0,8,9,11,10)

	#define HUGO_DECRYPT_DATA(_data) \
		BITSWAP8(_data,7,6,5,4,0,1,2,3)

	UINT8 *roml = image.get_software_region("roml");
	UINT8 *decrypted = image.device().machine().root_device().memregion("user1")->base();

	// decrypt ROMs
	for (offs_t offset = 0; offset < 0x20000; offset++)
	{
		offs_t address = (offset & 0x10000) | HUGO_DECRYPT_ADDRESS(offset);
		decrypted[address] = HUGO_DECRYPT_DATA(roml[offset]);
	}

	// map cartridge ROMs
	map_cartridge_roml(image.device().machine(), 0x0000);

	// install bankswitch handler
	install_io1_handler(hugo_bank_w);
}

static WRITE8_HANDLER( easy_calc_result_bank_w )
{
	map_cartridge_romh(space.machine(), 0x2000 + (!offset * 0x2000));
}

static void load_easy_calc_result_cartridge(device_image_interface &image)
{
	load_cartridge_region(image, "roml", 0x0000, 0x2000);
	load_cartridge_region(image, "romh", 0x2000, 0x4000);

	map_cartridge_roml(image.device().machine(), 0x0000);
	map_cartridge_romh(image.device().machine(), 0x2000);

	install_write_handler(0xde00, 0xde01, easy_calc_result_bank_w);
}

static WRITE8_HANDLER( pagefox_bank_w )
{
	/*

        Die 96KB des Moduls belegen in 6 16K-Banken den Modulbereich von $8000- $c000.
        Die Umschaltung erfolgt mit einem Register in $DE80 (-$DEFF, nicht voll decodiert),
        welches nur beschrieben und nicht gelesen werden kann. Durch Schreiben der Werte
        $08 oder $0A selektiert man eine der beiden RAM-Banke, $FF deselektiert das Modul.

        Zusatzlich muss Adresse 1 entsprechend belegt werden :$37 fur Lesezugriffe auf das
        Modul, $35 oder $34 fur Lesezugriffe auf das Ram des C64. Schreibzugriffe lenkt
        der C64 grundsatzlich ins eigene RAM, weshalb zum Beschreiben des Modulrams ein
        Trick notwendig ist: Man schaltet das Ram-Modul parallel zum C64-Ram, rettet vor
        dem Schreiben den C64-Ram-Inhalt und stellt ihn nachher wieder her...

        Ldy#0
        Lda#$35
        Sta 1
        Loop Lda (Ptr),y
        Pha
        Lda#$08
        Sta $DE80
        Lda (Quell),y
        Sta (Ptr),y
        Lda#$FF
        Sta $DE80
        Pla
        Sta (Ptr),y
        Iny
        Bne Loop

    */

	legacy_c64_state *state = space.machine().driver_data<legacy_c64_state>();
	UINT8 *cart = state->memregion("user1")->base();

	if (data == 0xff)
	{
		// hide cartridge
		state->m_game = 1;
		state->m_exrom = 1;
	}
	else
	{
		if (state->m_game)
		{
			// enable cartridge
			state->m_game = 0;
			state->m_exrom = 0;
		}

		int bank = (data >> 1) & 0x07;
		int ram = BIT(data, 3);
		offs_t address = bank * 0x4000;

		state->m_roml_writable = ram;

		if (ram)
		{
			state->m_roml = cart + address;
		}
		else
		{
			state->m_roml = state->m_c64_roml;

			map_cartridge_roml(space.machine(), address);
			map_cartridge_romh(space.machine(), address + 0x2000);
		}
	}

	c64_bankswitch(space.machine(), 0);
}

static void load_pagefox_cartridge(device_image_interface &image)
{
	load_cartridge_region(image, "rom", 0x0000, 0x10000);

	map_cartridge_roml(image.device().machine(), 0x0000);
	map_cartridge_romh(image.device().machine(), 0x2000);

	install_write_handler(0xde80, 0xdeff, pagefox_bank_w);
}

static WRITE8_HANDLER( multiscreen_bank_w )
{
	legacy_c64_state *state = space.machine().driver_data<legacy_c64_state>();
	UINT8 *cart = state->memregion("user1")->base();
	int bank = data & 0x0f;
	offs_t address = bank * 0x4000;

	if (bank == 0x0d)
	{
		// RAM
		state->m_roml = cart + address;
		state->m_roml_writable = 1;

		map_cartridge_romh(space.machine(), 0x2000);
	}
	else
	{
		// ROM
		state->m_roml = state->m_c64_roml;
		state->m_roml_writable = 0;

		map_cartridge_roml(space.machine(), address);
		map_cartridge_romh(space.machine(), address + 0x2000);
	}

	c64_bankswitch(space.machine(), 0);
}

static void load_multiscreen_cartridge(device_image_interface &image)
{
	load_cartridge_region(image, "roml", 0x0000, 0x4000);
	load_cartridge_region(image, "rom", 0x4000, 0x30000);

	map_cartridge_roml(image.device().machine(), 0x0000);
	map_cartridge_romh(image.device().machine(), 0x2000);

	install_write_handler(0xdfff, 0xdfff, multiscreen_bank_w);
}

static WRITE8_HANDLER( simons_basic_bank_w )
{
	set_game_line(space.machine(), !BIT(data, 0));
}

static void load_simons_basic_cartridge(device_image_interface &image)
{
	load_cartridge_region(image, "roml", 0x0000, 0x2000);
	load_cartridge_region(image, "romh", 0x2000, 0x2000);

	map_cartridge_roml(image.device().machine(), 0x0000);
	map_cartridge_romh(image.device().machine(), 0x2000);

	install_io1_handler(simons_basic_bank_w);
}

static READ8_HANDLER( super_explode_r )
{
	legacy_c64_state *state = space.machine().driver_data<legacy_c64_state>();

	return state->m_roml[0x1f00 | offset];
}

static WRITE8_HANDLER( super_explode_bank_w )
{
	map_cartridge_roml(space.machine(), BIT(data, 7) * 0x2000);
}

static void load_super_explode_cartridge(device_image_interface &image)
{
	load_cartridge_region(image, "roml", 0x0000, 0x4000);

	map_cartridge_roml(image.device().machine(), 0x0000);

	address_space &space = image.device().machine().firstcpu->space(AS_PROGRAM);
	space.install_legacy_read_handler(0xdf00, 0xdfff, FUNC(super_explode_r));

	install_io2_handler(super_explode_bank_w);
}

static void c64_software_list_cartridge_load(device_image_interface &image)
{
	legacy_c64_state *state = image.device().machine().driver_data<legacy_c64_state>();

	// initialize ROML and ROMH pointers
	state->m_roml = state->m_c64_roml;
	state->m_romh = state->m_c64_romh;

	// clear ROML and ROMH areas
	memset(state->m_roml, 0, 0x2000);
	memset(state->m_romh, 0, 0x2000);

	// set GAME and EXROM
	state->m_game = atol(image.get_feature("game"));
	state->m_exrom = atol(image.get_feature("exrom"));

	// determine cartridge type
	const char *cart_type = image.get_feature("cart_type");

	if (cart_type == NULL)
	{
		load_standard_c64_cartridge(image);
	}
	else
	{
		if (!strcmp(cart_type, "vizawrite"))
			load_vizawrite_cartridge(image);

		else if (!strcmp(cart_type, "hugo"))
			load_hugo_cartridge(image);

		else if (!strcmp(cart_type, "easy_calc_result"))
			load_easy_calc_result_cartridge(image);

		else if (!strcmp(cart_type, "pagefox"))
			load_pagefox_cartridge(image);

		else if (!strcmp(cart_type, "multiscreen"))
			/*

                TODO: crashes on protection check after cartridge RAM test

                805A: lda  $01
                805C: and  #$FE
                805E: sta  $01
                8060: m6502_brk#$00 <-- BOOM!

            */
			load_multiscreen_cartridge(image);

		else if (!strcmp(cart_type, "simons_basic"))
			load_simons_basic_cartridge(image);

		else if (!strcmp(cart_type, "super_explode"))
			load_super_explode_cartridge(image);

		else
			load_standard_c64_cartridge(image);
	}
}

static DEVICE_IMAGE_LOAD( c64_cart )
{
	int result = IMAGE_INIT_PASS;

	if (image.software_entry() != NULL)
	{
		c64_software_list_cartridge_load(image);
	}
	else
		result = c64_crt_load(image);

	return result;
}
MACHINE_CONFIG_FRAGMENT( c64_cartslot )
	MCFG_CARTSLOT_ADD("cart1")
	MCFG_CARTSLOT_EXTENSION_LIST("crt,80")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("c64_cart")
	MCFG_CARTSLOT_START(c64_cart)
	MCFG_CARTSLOT_LOAD(c64_cart)
	MCFG_CARTSLOT_UNLOAD(c64_cart)

	MCFG_CARTSLOT_ADD("cart2")
	MCFG_CARTSLOT_EXTENSION_LIST("crt,80")
	MCFG_CARTSLOT_NOT_MANDATORY
	MCFG_CARTSLOT_START(c64_cart)
	MCFG_CARTSLOT_LOAD(c64_cart)
	MCFG_CARTSLOT_UNLOAD(c64_cart)

	MCFG_SOFTWARE_LIST_ADD("cart_list_c64", "c64_cart")
MACHINE_CONFIG_END

