/***************************************************************************

prelim notes:
Flipper Jack, by Jackson, 198?
probably a prequel/sequel to superwng

xtal: 16mhz, 6mhz
cpu: 2*z80
sound: 2*ay8910
other: 8255 ppi, hd6845 crtc, 1 dipsw
ram: 2*8KB, 4*2KB
rom: see romdefs


***************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"
#include "machine/i8255.h"
#include "sound/ay8910.h"
#include "video/mc6845.h"

#define MASTER_CLOCK	XTAL_16MHz
#define VIDEO_CLOCK		XTAL_6MHz


class flipjack_state : public driver_device
{
public:
	flipjack_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_audiocpu(*this, "audiocpu"),
		m_crtc(*this, "crtc"),
		m_fbram(*this, "fb_ram"),
		m_vram(*this, "vram"),
		m_cram(*this, "cram")
	{
		m_soundlatch = 0;
	}

	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_audiocpu;
	required_device<hd6845_device> m_crtc;

	required_shared_ptr<UINT8> m_fbram;
	required_shared_ptr<UINT8> m_vram;
	required_shared_ptr<UINT8> m_cram;

	UINT8 m_soundlatch;

	DECLARE_WRITE8_MEMBER(flipjack_sound_nmi_ack_w);
	DECLARE_WRITE8_MEMBER(flipjack_soundlatch_w);
	DECLARE_WRITE8_MEMBER(flipjack_unk_w);
	DECLARE_INPUT_CHANGED_MEMBER(flipjack_coin);

};




static SCREEN_UPDATE_RGB32( flipjack )
{
	flipjack_state *state = screen.machine().driver_data<flipjack_state>();
	int x,y,count;
	const UINT8 *blit_ram = state->memregion("gfx2")->base();

	bitmap.fill(get_black_pen(screen.machine()), cliprect);

	count = (0);

	for(y=0;y<192;y++)
	{
		for(x=0;x<256;x+=8)
		{
			UINT32 pen_r,pen_g,pen_b,color;
			int xi;

			pen_r = (blit_ram[count] & 0xff)>>0;
			pen_g = (blit_ram[count+0x2000] & 0xff)>>0;
			pen_b = (blit_ram[count+0x4000] & 0xff)>>0;

			for(xi=0;xi<8;xi++)
			{
				if(cliprect.contains(x+xi, y))
				{
					color = ((pen_r >> (7-xi)) & 1);
					color|= ((pen_g >> (7-xi)) & 1)<<1;
					color|= ((pen_b >> (7-xi)) & 1)<<2;
					bitmap.pix32(y, x+xi) = screen.machine().pens[color];
				}
			}

			count++;
		}
	}

	count = 0;

	for(y=0;y<192;y++)
	{
		for(x=0;x<256;x+=8)
		{
			UINT32 pen,color;
			int xi;

			pen = (state->m_fbram[count] & 0xff)>>0;

			for(xi=0;xi<8;xi++)
			{
				if(cliprect.contains(x+xi, y))
				{
					color = ((pen >> (7-xi)) & 1) ? 7 : 0;
					if(color)
						bitmap.pix32(y, x+xi) = screen.machine().pens[color];
				}
			}

			count++;
		}
	}

	for (y=0;y<32;y++)
	{
		for (x=0;x<32;x++)
		{
			const gfx_element *gfx = screen.machine().gfx[0];
			UINT16 tile = state->m_vram[x+y*0x100];

			drawgfx_transpen(bitmap,cliprect,gfx,tile,0,0,0,x*8,(y*8),0);
		}
	}

	return 0;
}

WRITE8_MEMBER(flipjack_state::flipjack_sound_nmi_ack_w)
{
	device_set_input_line(m_audiocpu, INPUT_LINE_NMI, CLEAR_LINE);
}

WRITE8_MEMBER(flipjack_state::flipjack_soundlatch_w)
{
	m_soundlatch = data;
	device_set_input_line(m_audiocpu, 0, ASSERT_LINE);
}

WRITE8_MEMBER(flipjack_state::flipjack_unk_w)
{
	// banking related?
}

static READ8_DEVICE_HANDLER( flipjack_soundlatch_r )
{
	flipjack_state *state = device->machine().driver_data<flipjack_state>();
	device_set_input_line(state->m_audiocpu, 0, CLEAR_LINE);
	return state->m_soundlatch;
}


static WRITE8_DEVICE_HANDLER( flipjack_portc_w )
{
	//flipjack_state *state = device->machine().driver_data<flipjack_state>();
	//printf("port c   %X\n",data>>4);
}


INPUT_CHANGED_MEMBER(flipjack_state::flipjack_coin)
{
	if (newval)
		device_set_input_line(m_maincpu, INPUT_LINE_NMI, PULSE_LINE);
}


static ADDRESS_MAP_START( flipjack_main_map, AS_PROGRAM, 8, flipjack_state )
	AM_RANGE(0x0000, 0x3fff) AM_ROM
	AM_RANGE(0x4000, 0x5fff) AM_RAM
	AM_RANGE(0x6000, 0x67ff) AM_RAM
	AM_RANGE(0x6800, 0x6803) AM_DEVREADWRITE("ppi8255", i8255_device, read, write)
	AM_RANGE(0x7000, 0x7000) AM_WRITE(flipjack_soundlatch_w)
	AM_RANGE(0x7010, 0x7010) AM_DEVWRITE("crtc", hd6845_device, address_w)
	AM_RANGE(0x7011, 0x7011) AM_DEVWRITE("crtc", hd6845_device, register_w)
	AM_RANGE(0x7020, 0x7020) AM_READ_PORT("DSW")
	AM_RANGE(0x8000, 0x9fff) AM_ROM
	AM_RANGE(0xa000, 0xbfff) AM_RAM AM_SHARE("cram")
	AM_RANGE(0xc000, 0xdfff) AM_RAM AM_SHARE("vram")
	AM_RANGE(0xe000, 0xffff) AM_RAM AM_SHARE("fb_ram")
ADDRESS_MAP_END

static ADDRESS_MAP_START( flipjack_main_io_map, AS_IO, 8, flipjack_state )
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0xff, 0xff) AM_WRITE(flipjack_unk_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( flipjack_sound_map, AS_PROGRAM, 8, flipjack_state )
	AM_RANGE(0x0000, 0x1fff) AM_ROM
	AM_RANGE(0x2000, 0x27ff) AM_RAM
	AM_RANGE(0x4000, 0x4000) AM_DEVREADWRITE_LEGACY("ay2", ay8910_r, ay8910_data_w)
	AM_RANGE(0x6000, 0x6000) AM_DEVWRITE_LEGACY("ay2", ay8910_address_w)
	AM_RANGE(0x8000, 0x8000) AM_DEVREADWRITE_LEGACY("ay1", ay8910_r, ay8910_data_w)
	AM_RANGE(0xa000, 0xa000) AM_DEVWRITE_LEGACY("ay1", ay8910_address_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( flipjack_sound_io_map, AS_IO, 8, flipjack_state )
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x00, 0x00) AM_WRITE(flipjack_sound_nmi_ack_w)
ADDRESS_MAP_END


static INPUT_PORTS_START( flipjack )
	PORT_START("COIN")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_COIN1 ) PORT_CHANGED_MEMBER(DEVICE_SELF, flipjack_state, flipjack_coin, 0)

	PORT_START("P1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("P1 Launch Ball")
	PORT_DIPUNKNOWN_DIPLOC( 0x02, 0x02, "P1:2" ) // P1 Left Flipper?
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x04, "P1:3" ) // P1 Tilt?
	PORT_DIPUNKNOWN_DIPLOC( 0x08, 0x08, "P1:4" ) // P1 Right Flipper?
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_DIPUNKNOWN_DIPLOC( 0x20, 0x20, "P1:6" )
	PORT_DIPUNKNOWN_DIPLOC( 0x40, 0x40, "P1:7" )
	PORT_DIPUNKNOWN_DIPLOC( 0x80, 0x80, "P1:8" )

	PORT_START("P2")
	PORT_DIPUNKNOWN_DIPLOC( 0x01, 0x01, "P2:1" ) // P2 Launch Ball?
	PORT_DIPUNKNOWN_DIPLOC( 0x02, 0x02, "P2:2" ) // P2 Left Flipper?
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x04, "P2:3" ) // P2 Tilt?
	PORT_DIPUNKNOWN_DIPLOC( 0x08, 0x08, "P2:4" ) // P2 Right Flipper?
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START2 )
	PORT_DIPUNKNOWN_DIPLOC( 0x20, 0x20, "P2:6" )
	PORT_DIPUNKNOWN_DIPLOC( 0x40, 0x40, "P2:7" )
	PORT_DIPUNKNOWN_DIPLOC( 0x80, 0x80, "P2:8" )

	PORT_START("P3")
	PORT_DIPUNKNOWN_DIPLOC( 0x01, 0x01, "P3:1" ) // coin?
	PORT_DIPUNKNOWN_DIPLOC( 0x02, 0x02, "P3:2" )
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x04, "P3:3" )
	PORT_DIPUNKNOWN_DIPLOC( 0x08, 0x08, "P3:4" )
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNUSED ) // output

	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x00, DEF_STR( Demo_Sounds ) )	PORT_DIPLOCATION("A0:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Coinage ) )		PORT_DIPLOCATION("A0:2")
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_2C ) )
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x04, "A0:3" ) // drop target
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Cabinet ) )		PORT_DIPLOCATION("A0:4")
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x10, 0x00, DEF_STR( Lives ) )		PORT_DIPLOCATION("A0:5")
	PORT_DIPSETTING(    0x00, "3" )
	PORT_DIPSETTING(    0x10, "5" )
	PORT_DIPUNKNOWN_DIPLOC( 0x20, 0x20, "A0:6" ) // extra lives
	PORT_DIPUNKNOWN_DIPLOC( 0x40, 0x40, "A0:7" ) // "
	PORT_DIPUNKNOWN_DIPLOC( 0x80, 0x80, "A0:8" ) // "

INPUT_PORTS_END


static I8255A_INTERFACE( ppi8255_intf )
{
	DEVCB_INPUT_PORT("P1"),				/* Port A read */
	DEVCB_NULL,							/* Port A write */
	DEVCB_INPUT_PORT("P2"),				/* Port B read */
	DEVCB_NULL,							/* Port B write */
	DEVCB_INPUT_PORT("P3"),				/* Port C read */
	DEVCB_HANDLER(flipjack_portc_w)		/* Port C write */
};


static AY8910_INTERFACE( ay8910_config_1 )
{
	AY8910_LEGACY_OUTPUT,					/* Flags */
	AY8910_DEFAULT_LOADS,					/* Load on channel in ohms */
	DEVCB_HANDLER(flipjack_soundlatch_r),	/* Port A read */
	DEVCB_NULL,								/* Port B read */
	DEVCB_NULL,								/* Port A write */
	DEVCB_NULL								/* Port B write */
};

static AY8910_INTERFACE( ay8910_config_2 )
{
	AY8910_LEGACY_OUTPUT,					/* Flags */
	AY8910_DEFAULT_LOADS,					/* Load on channel in ohms */
	DEVCB_NULL,								/* Port A read */
	DEVCB_NULL,								/* Port B read */
	DEVCB_NULL,								/* Port A write */
	DEVCB_NULL								/* Port B write */
};

static MC6845_INTERFACE( mc6845_intf )
{
	"screen",	/* screen we are acting on */
	8,			/* number of pixels per video memory address */
	NULL,		/* before pixel update callback */
	NULL,		/* row update callback */
	NULL,		/* after pixel update callback */
	DEVCB_NULL,	/* callback for display state changes */
	DEVCB_NULL,	/* callback for cursor state changes */
	DEVCB_NULL,	/* HSYNC callback */
	DEVCB_NULL,	/* VSYNC callback */
	NULL		/* update address callback */
};




static const gfx_layout tilelayout =
{
	8, 8,
	RGN_FRAC(1,1),
	1,
	{ 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8
};


static GFXDECODE_START( flipjack )
	GFXDECODE_ENTRY( "gfx1", 0, tilelayout, 0, 2 )
GFXDECODE_END




static MACHINE_START( flipjack )
{
	//flipjack_state *state = machine.driver_data<flipjack_state>();
}


static MACHINE_CONFIG_START( flipjack, flipjack_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", Z80, MASTER_CLOCK/4)
	MCFG_CPU_PROGRAM_MAP(flipjack_main_map)
	MCFG_CPU_IO_MAP(flipjack_main_io_map)
	MCFG_CPU_VBLANK_INT("screen", irq0_line_hold)

	MCFG_CPU_ADD("audiocpu", Z80, MASTER_CLOCK/4)
	MCFG_CPU_PROGRAM_MAP(flipjack_sound_map)
	MCFG_CPU_IO_MAP(flipjack_sound_io_map)
	MCFG_CPU_VBLANK_INT("screen", nmi_line_assert)

	MCFG_I8255A_ADD( "ppi8255", ppi8255_intf )

	MCFG_MACHINE_START(flipjack)

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS(VIDEO_CLOCK, 0x188, 0, 0x100, 0x100, 0, 0xc0) // from crtc

	MCFG_PALETTE_LENGTH(256)
	MCFG_SCREEN_UPDATE_STATIC(flipjack)

	MCFG_GFXDECODE(flipjack)

	MCFG_MC6845_ADD("crtc", HD6845, VIDEO_CLOCK/8, mc6845_intf)


	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("ay1", AY8910, MASTER_CLOCK/8)
	MCFG_SOUND_CONFIG(ay8910_config_1)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)

	MCFG_SOUND_ADD("ay2", AY8910, MASTER_CLOCK/8)
	MCFG_SOUND_CONFIG(ay8910_config_2)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.50)
MACHINE_CONFIG_END

ROM_START( flipjack )
	ROM_REGION( 0x12000, "maincpu", 0 )
	ROM_LOAD( "3.d5",  0x0000, 0x2000, CRC(123bd992) SHA1(d845e2b9af5b81d950e5edf35201f1dd1c4af651) )
	ROM_LOAD( "2.m5",  0x2000, 0x2000, CRC(e2bdce13) SHA1(50d990095a35837570b3117763e990440d8656ae) )
	ROM_LOAD( "1.l5",  0x4000, 0x2000, CRC(4632263b) SHA1(b1fbb851ffd8aff36aff6f36672122fef3dd0af1) ) // what's this rom?
	ROM_LOAD( "4.f5",  0x8000, 0x2000, CRC(d27e0184) SHA1(f108993fc3fce9173a4961a76fc60655fdd1cd25) )

	ROM_REGION( 0x2000, "audiocpu", 0 )
	ROM_LOAD( "s.s5",  0x0000, 0x2000, CRC(34515a7b) SHA1(affe34198b77bddd314fae2851fd6a29d80f734e) )

	ROM_REGION( 0x2000, "gfx1", 0 )
	ROM_LOAD( "cg.l6", 0x0000, 0x2000, CRC(8d87f6b9) SHA1(55ca726f190eac9ee7e26b8f4e519f1634bec0dd) )

	ROM_REGION( 0x6000, "gfx2", 0 )
	ROM_LOAD( "r.f6",  0x0000, 0x2000, CRC(8c02fe71) SHA1(148e7382dc9b7678c447ada5ad19e03a3a051a7f) )
	ROM_LOAD( "g.d6",  0x2000, 0x2000, CRC(8624d07f) SHA1(fb51c9c785d56854a6530b71868e95ad6be7cbee) )
	ROM_LOAD( "b.h6",  0x4000, 0x2000, CRC(bbc8fdcc) SHA1(93758ca13cc49b87508f01c86c652155945dd484) )

	ROM_REGION( 0x0100, "proms", 0 )
	ROM_LOAD( "m3-7611-5.f8", 0x0000, 0x0100, CRC(f0248102) SHA1(22d87935c941e2e8bba5427599f6fd5fa1262ebc) )
ROM_END




GAME( 198?, flipjack,   0,      flipjack, flipjack, 0, ROT90, "Jackson Co., Ltd.", "Flipper Jack", GAME_NOT_WORKING )