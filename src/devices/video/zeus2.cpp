// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/*************************************************************************

    Midway Zeus2 Video

**************************************************************************/
#include "zeus2.h"

#define LOG_REGS         1

/*************************************
*  Constructor
*************************************/
zeus2_renderer::zeus2_renderer(zeus2_device *state)
	: poly_manager<float, zeus2_poly_extra_data, 4, 10000>(state->machine())
	, m_state(state)
{
}

const device_type ZEUS2 = &device_creator<zeus2_device>;

zeus2_device::zeus2_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: device_t(mconfig, ZEUS2, "Midway Zeus2", tag, owner, clock, "zeus2", __FILE__),
	m_vblank(*this), m_irq(*this), m_atlantis(0)
{
}

/*************************************
*  Display interrupt generation
*************************************/

TIMER_CALLBACK_MEMBER(zeus2_device::display_irq_off)
{
	m_vblank(CLEAR_LINE);

	//attotime vblank_period = m_screen->time_until_pos(m_zeusbase[0x37] & 0xffff);

	///* if zero, adjust to next frame, otherwise we may get stuck in an infinite loop */
	//if (vblank_period == attotime::zero)
	//  vblank_period = m_screen->frame_period();
	//vblank_timer->adjust(vblank_period);
	vblank_timer->adjust(m_screen->time_until_vblank_start());
	//machine().scheduler().timer_set(attotime::from_hz(30000000), timer_expired_delegate(FUNC(zeus2_device::display_irq), this));
}

TIMER_CALLBACK_MEMBER(zeus2_device::display_irq)
{
	m_vblank(ASSERT_LINE);
	/* set a timer for the next off state */
	//machine().scheduler().timer_set(m_screen->time_until_pos(0), timer_expired_delegate(FUNC(zeus2_device::display_irq_off), this), 0, this);
	machine().scheduler().timer_set(m_screen->time_until_vblank_end(), timer_expired_delegate(FUNC(zeus2_device::display_irq_off), this), 0, this);
	//machine().scheduler().timer_set(attotime::from_hz(30000000), timer_expired_delegate(FUNC(zeus2_device::display_irq_off), this));
}

TIMER_CALLBACK_MEMBER(zeus2_device::int_timer_callback)
{
	//m_maincpu->set_input_line(2, ASSERT_LINE);
	m_irq(ASSERT_LINE);
}

/*************************************
 *  Video startup
 *************************************/


void zeus2_device::device_start()
{
	/* allocate memory for "wave" RAM */
	waveram[0] = auto_alloc_array(machine(), UINT32, WAVERAM0_WIDTH * WAVERAM0_HEIGHT * 8/4);
	//waveram[1] = auto_alloc_array(machine(), UINT32, WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 12/4);
	m_frameColor = std::make_unique<UINT32[]>(WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 2);
	m_frameDepth = std::make_unique<UINT16[]>(WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 2);

	/* initialize polygon engine */
	poly = auto_alloc(machine(), zeus2_renderer(this));

	//m_screen = machine().first_screen();
	m_screen = downcast<screen_device *>(machine().device("screen"));
	m_vblank.resolve_safe();
	m_irq.resolve_safe();

	/* we need to cleanup on exit */
	//machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(FUNC(zeus2_device::exit_handler2), this));

	int_timer = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(zeus2_device::int_timer_callback), this));
	vblank_timer = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(zeus2_device::display_irq), this));

	/* save states */
	save_pointer(NAME(waveram[0]), WAVERAM0_WIDTH * WAVERAM0_HEIGHT * 8 / sizeof(waveram[0][0]));
	//save_pointer(NAME(waveram[1]), WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 12 / sizeof(waveram[1][0]));
	save_pointer(m_frameColor.get(), "m_frameColor", sizeof(m_frameColor[0]) * WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 2);
	save_pointer(m_frameDepth.get(), "m_frameDepth", sizeof(m_frameDepth[0]) * WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 2);
	save_pointer(NAME(m_zeusbase), sizeof(m_zeusbase[0]) * 0x80);
	save_pointer(NAME(m_renderRegs), sizeof(m_renderRegs[0]) * 0x40);
	save_item(NAME(zeus_fifo));
	save_item(NAME(zeus_fifo_words));
	save_item(NAME(zeus_cliprect.min_x));
	save_item(NAME(zeus_cliprect.max_x));
	save_item(NAME(zeus_cliprect.min_y));
	save_item(NAME(zeus_cliprect.max_y));
	save_item(NAME(zeus_matrix));
	save_item(NAME(zeus_point));
	save_item(NAME(zeus_texbase));
	save_item(NAME(m_fill_color));
	save_item(NAME(m_fill_depth));
	save_item(NAME(m_renderPtr));
}

void zeus2_device::device_reset()
{
	memset(m_zeusbase, 0, sizeof(m_zeusbase[0]) * 0x80);
	memset(m_renderRegs, 0, sizeof(m_renderRegs[0]) * 0x40);

	zbase = 32.0f;
	m_yScale = 0;
	yoffs = 0x1dc000;
	//yoffs = 0x00040000;
	texel_width = 256;
	zeus_fifo_words = 0;
	m_fill_color = 0;
	m_fill_depth = 0;
	m_renderPtr = 0;
}

void zeus2_device::device_stop()
{
#if DUMP_WAVE_RAM
	FILE *f = fopen("waveram.dmp", "w");
	int i;

	for (i = 0; i < WAVERAM0_WIDTH * WAVERAM0_HEIGHT; i++)
	{
		if (i % 4 == 0) fprintf(f, "%03X%03X: ", i / WAVERAM0_WIDTH, i % WAVERAM0_WIDTH);
		fprintf(f, " %08X %08X ",
			WAVERAM_READ32(waveram[0], i*2+0),
			WAVERAM_READ32(waveram[0], i*2+1));
		if (i % 4 == 3) fprintf(f, "\n");
	}
	fclose(f);
#endif

#if TRACK_REG_USAGE
{
	reg_info *info;
	int regnum;

	for (regnum = 0; regnum < 0x80; regnum++)
	{
		printf("Register %02X\n", regnum);
		if (regread_count[regnum] == 0)
			printf("\tNever read\n");
		else
			printf("\tRead %d times\n", regread_count[regnum]);

		if (regwrite_count[regnum] == 0)
			printf("\tNever written\n");
		else
		{
			printf("\tWritten %d times\n", regwrite_count[regnum]);
			for (info = regdata[regnum]; info != nullptr; info = info->next)
				printf("\t%08X\n", info->value);
		}
	}

	for (regnum = 0; regnum < 0x100; regnum++)
		if (subregwrite_count[regnum] != 0)
		{
			printf("Sub-Register %02X (%d writes)\n", regnum, subregwrite_count[regnum]);
			for (info = subregdata[regnum]; info != nullptr; info = info->next)
				printf("\t%08X\n", info->value);
		}
}
#endif

}



/*************************************
 *
 *  Video update
 *
 *************************************/

UINT32 zeus2_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	// Wait until configuration is completed before transfering anything
	if (m_zeusbase[0x30] == 0)
		return 0;

	int x, y;

	poly->wait();

	if (machine().input().code_pressed(KEYCODE_DOWN)) { zbase += machine().input().code_pressed(KEYCODE_LSHIFT) ? 0x10 : 1; popmessage("Zbase = %f", (double)zbase); }
	if (machine().input().code_pressed(KEYCODE_UP)) { zbase -= machine().input().code_pressed(KEYCODE_LSHIFT) ? 0x10 : 1; popmessage("Zbase = %f", (double)zbase); }

	/* normal update case */
	if (!machine().input().code_pressed(KEYCODE_W))
	{
		int xoffs = screen.visible_area().min_x;
		for (y = cliprect.min_y; y <= cliprect.max_y; y++)
		{
			UINT32 *colorptr = &m_frameColor[frame_addr_from_xy(0, y, false)];
			UINT32 *dest = &bitmap.pix32(y);
			for (x = cliprect.min_x; x <= cliprect.max_x; x++) {
				UINT32 bufX = x - xoffs;
				//dest[x] = WAVERAM_READPIX(base, y, x - xoffs);
				dest[x] = colorptr[bufX];
			}
		}
	}

	/* waveram drawing case */
	else
	{
		const void *base;

		if (machine().input().code_pressed(KEYCODE_DOWN)) yoffs += machine().input().code_pressed(KEYCODE_LSHIFT) ? 0x1000 : 40;
		if (machine().input().code_pressed(KEYCODE_UP)) yoffs -= machine().input().code_pressed(KEYCODE_LSHIFT) ? 0x1000 : 40;
		if (machine().input().code_pressed(KEYCODE_LEFT) && texel_width > 4) { texel_width >>= 1; while (machine().input().code_pressed(KEYCODE_LEFT)) ; }
		if (machine().input().code_pressed(KEYCODE_RIGHT) && texel_width < 512) { texel_width <<= 1; while (machine().input().code_pressed(KEYCODE_RIGHT)) ; }

		if (yoffs < 0) yoffs = 0;
		if (1) {
			//base = waveram0_ptr_from_expanded_addr(yoffs << 8);
			//base = waveram0_ptr_from_expanded_addr(yoffs);
			base = WAVERAM_BLOCK0(yoffs);
		}
		else
			base = (void *)&m_frameColor[yoffs << 6];

		int xoffs = screen.visible_area().min_x;
		for (y = cliprect.min_y; y <= cliprect.max_y; y++)
		{
			UINT32 *dest = &bitmap.pix32(y);
			for (x = cliprect.min_x; x <= cliprect.max_x; x++)
			{
				if (1) {
					UINT8 tex = get_texel_8bit((UINT64 *)base, y, x, texel_width);
					dest[x] = (tex << 16) | (tex << 8) | tex;
				}
				else {
					dest[x] = ((UINT32 *)(base))[((y * WAVERAM1_WIDTH)) + x - xoffs];
				}
			}
		}
		popmessage("offs = %06X base = %08X", yoffs, base);
	}

	return 0;
}



/*************************************
 *
 *  Core read handler
 *
 *************************************/

READ32_MEMBER( zeus2_device::zeus2_r )
{
	int logit = (offset != 0x00 && offset != 0x01 &&
		offset != 0x48 && offset != 0x49 &&
		offset != 0x54 && offset != 0x58 && offset != 0x59 && offset != 0x5a);
	logit &= LOG_REGS;
	UINT32 result = m_zeusbase[offset];
#if TRACK_REG_USAGE
	regread_count[offset]++;
#endif

	switch (offset)
	{
		case 0x00:
			result = 0x20;
			break;

		case 0x01:
			/* bit  $000C0070 are tested in a loop until 0 */
			/* bits $00080000 is tested in a loop until 0 */
			/* bit  $00000004 is tested for toggling; probably VBLANK */
			result = 0x00;
			if (m_screen->vblank())
				result |= 0x04;
			break;

		case 0x07:
			/* this is needed to pass the self-test in thegrid */
			result = 0x10451998;
			break;

		case 0x54:
			/* both upper 16 bits and lower 16 bits seem to be used as vertical counters */
			result = (m_screen->vpos() << 16) | m_screen->vpos();
			break;
	}

	if (logit)
		logerror("%08X:zeus2_r(%02X) = %08X\n", machine().device("maincpu")->safe_pc(), offset, result);

	return result;
}



/*************************************
 *
 *  Core write handler
 *
 *************************************/

WRITE32_MEMBER( zeus2_device::zeus2_w )
{
	int logit = (offset != 0x08 &&
					(offset != 0x20 || data != 0) &&
					offset != 0x40 && offset != 0x41 && offset != 0x48 && offset != 0x49 && offset != 0x4e &&
					offset != 0x50 && offset != 0x51 && offset != 0x57 && offset != 0x58 && offset != 0x59 && offset != 0x5a && offset != 0x5e
		);
	logit &= LOG_REGS;
	if (logit)
		logerror("%08X:zeus2_w", machine().device("maincpu")->safe_pc());
	zeus2_register32_w(offset, data, logit);
}



/*************************************
 *
 *  Handle register writes
 *
 *************************************/

void zeus2_device::zeus2_register32_w(offs_t offset, UINT32 data, int logit)
{
	UINT32 oldval = m_zeusbase[offset];

#if TRACK_REG_USAGE
regwrite_count[offset]++;
if (regdata_count[offset] < 256)
{
	reg_info **tailptr;

	for (tailptr = &regdata[offset]; *tailptr != nullptr; tailptr = &(*tailptr)->next)
		if ((*tailptr)->value == data)
			break;
	if (*tailptr == nullptr)
	{
		*tailptr = alloc_or_die(reg_info);
		(*tailptr)->next = nullptr;
		(*tailptr)->value = data;
		regdata_count[offset]++;
	}
}
#endif

	/* writes to register $CC need to force a partial update */
//  if ((offset & ~1) == 0xcc)
//      m_screen->update_partial(m_screen->vpos());

	/* always write to low word? */
	m_zeusbase[offset] = data;

	/* log appropriately */
	if (logit) {
		logerror("(%02X) = %08X", offset, data);
	}
	/* handle the update */
	zeus2_register_update(offset, oldval, logit);
}



/*************************************
 *
 *  Update state after a register write
 *
 *************************************/

void zeus2_device::zeus2_register_update(offs_t offset, UINT32 oldval, int logit)
{
	/* handle the writes; only trigger on low accesses */
	switch (offset)
	{
		case 0x08:
			zeus_fifo[zeus_fifo_words++] = m_zeusbase[0x08];
			if (zeus2_fifo_process(zeus_fifo, zeus_fifo_words))
				zeus_fifo_words = 0;

			/* set the interrupt signal to indicate we can handle more */
			int_timer->adjust(attotime::from_nsec(500));
			break;

		case 0x20:
			/* toggles between two values based on the page:

			    Page #      m_zeusbase[0x20]      m_zeusbase[0x38]
			    ------      --------------      --------------
			       0          $04000190           $00000000
			       1          $04000000           $01900000
			*/
			zeus2_pointer_write(m_zeusbase[0x20] >> 24, (m_zeusbase[0x20] & 0xffffff), logit);
			break;

		case 0x30:
			{
				m_yScale = (((m_zeusbase[0x39] >> 16) & 0xfff) < 0x100) ? 0 : 1;
				int hor = ((m_zeusbase[0x34] & 0xffff) - (m_zeusbase[0x33] >> 16)) << m_yScale;
				int ver = ((m_zeusbase[0x35] & 0xffff) + 1) << m_yScale;
				popmessage("reg[30]: %08X Screen: %dH X %dV yScale: %d", m_zeusbase[0x30], hor, ver, m_yScale);
			}
			m_screen->update_partial(m_screen->vpos());
			{
				int vtotal = (m_zeusbase[0x37] & 0xffff) << m_yScale;
				int htotal = (m_zeusbase[0x34] >> 16) << m_yScale;
				rectangle visarea((m_zeusbase[0x33] >> 16) << m_yScale, htotal - 1, 0, (m_zeusbase[0x35] & 0xffff) << m_yScale);
				if (htotal > 0 && vtotal > 0 && visarea.min_x < visarea.max_x && visarea.max_y < vtotal)
				{
					m_screen->configure(htotal, vtotal, visarea, HZ_TO_ATTOSECONDS((double)ZEUS2_VIDEO_CLOCK / 4.0 / (htotal * vtotal)));
					zeus_cliprect = visarea;
					zeus_cliprect.max_x -= zeus_cliprect.min_x;
					zeus_cliprect.min_x = 0;
					// Startup vblank timer
					vblank_timer->adjust(attotime::from_usec(1));
				}
			}
			break;

		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
			break;

		case 0x38:
			{
				UINT32 temp = m_zeusbase[0x38];
				m_zeusbase[0x38] = oldval;
				m_screen->update_partial(m_screen->vpos());
				log_fifo = machine().input().code_pressed(KEYCODE_L);
				log_fifo = 1;
				m_zeusbase[0x38] = temp;
			}
			break;

		case 0x40:
			/* in direct mode it latches values */
			if ((m_zeusbase[0x4e] & 0x20) && m_zeusbase[0x40] == 0x00820000)
			{
				const void *src = waveram0_ptr_from_expanded_addr(m_zeusbase[0x41]);
				m_zeusbase[0x48] = WAVERAM_READ32(src, 0);
				m_zeusbase[0x49] = WAVERAM_READ32(src, 1);

				if (m_zeusbase[0x4e] & 0x40)
				{
					m_zeusbase[0x41]++;
					m_zeusbase[0x41] += (m_zeusbase[0x41] & 0x400) << 6;
					m_zeusbase[0x41] &= ~0xfc00;
				}
			}
			break;
		case 0x41:
			/* this is the address, except in read mode, where it latches values */
			if (m_zeusbase[0x4e] & 0x10)
			{
				const void *src = waveram0_ptr_from_expanded_addr(oldval);
				m_zeusbase[0x41] = oldval;
				m_zeusbase[0x48] = WAVERAM_READ32(src, 0);
				m_zeusbase[0x49] = WAVERAM_READ32(src, 1);

				if (m_zeusbase[0x4e] & 0x40)
				{
					m_zeusbase[0x41]++;
					m_zeusbase[0x41] += (m_zeusbase[0x41] & 0x400) << 6;
					m_zeusbase[0x41] &= ~0xfc00;
				}
			} else {
				// mwskinsa (atlantis) writes 0xffffffff and expects 0x1fff03ff to be read back
				m_zeusbase[0x41] &= 0x1fff03ff;
			}
			break;

		case 0x48:
		case 0x49:
			/* if we're in write mode, process it */
			if (m_zeusbase[0x40] == 0x00890000)
			{
				/*
				    m_zeusbase[0x4e]:
				        bit 0-1: which register triggers write through
				        bit 3:   enable write through via these registers
				        bit 4:   seems to be set during reads, when 0x41 is used for latching
				        bit 6:   enable autoincrement on write through
				*/
				if ((m_zeusbase[0x4e] & 0x08) && (offset & 3) == (m_zeusbase[0x4e] & 3))
				{
					void *dest = waveram0_ptr_from_expanded_addr(m_zeusbase[0x41]);
					WAVERAM_WRITE32(dest, 0, m_zeusbase[0x48]);
					WAVERAM_WRITE32(dest, 1, m_zeusbase[0x49]);

					if (m_zeusbase[0x4e] & 0x40)
					{
						m_zeusbase[0x41]++;
						m_zeusbase[0x41] += (m_zeusbase[0x41] & 0x400) << 6;
						m_zeusbase[0x41] &= ~0xfc00;
					}
				}
			}

			/* make sure we log anything else */
			else if (logit)
				logerror("\t[40]=%08X [4E]=%08X\n", m_zeusbase[0x40], m_zeusbase[0x4e]);
			break;

		case 0x50:
			if (m_zeusbase[0x50] == 0x00510000) {
				// SGRAM Special Mode Register Write
				if (m_zeusbase[0x51] == 0x00200000) {
					// SGRAM Mask Register
					if ((m_zeusbase[0x58] & m_zeusbase[0x59] & m_zeusbase[0x5a]) != 0xffffffff)
						logerror("zeus2_register_update: Warning! Mask Register not equal to 0xffffffff\n");
				}
				if (m_zeusbase[0x51] == 0x00400000) {
					// SGRAM Color Register
					m_fill_color = m_zeusbase[0x58];
					m_fill_depth = m_zeusbase[0x5a];
					if (m_zeusbase[0x58] != m_zeusbase[0x59])
						logerror("zeus2_register_update: Warning! Different fill colors are set.\n");
				}
			}
			//else if (1 && ((m_zeusbase[0x50] & 0x000f0000)==0x80000) && (m_zeusbase[0x50] & 0xffff)) {
			else if ((m_zeusbase[0x50] &  0x80000) && (m_zeusbase[0x50] & 0xffff)) {
				// Fast fill
				// Unknown what the exact bit fields are, this is a just a guess
				// Atlantis: 0x00983FFF => clear entire frame buffer, 0x00981FFF => clear one frame
				// crusnexo: 0x007831FF => clear one frame
				// thegrid:  0x008831FF => clear one frame
				// thegrid:  0x0079FFFF => clear entire frame buffer at 51=0 then 51=00800000, only seen at initial tests in thegrid
				UINT32 addr = frame_addr_from_phys_addr(m_zeusbase[0x51]);
				UINT32 numBytes = (m_zeusbase[0x50] & 0xffff) + 1;
				numBytes *= 0x40;
				if (m_zeusbase[0x50] & 0x10000) {
					addr = 0x0;
					numBytes = WAVERAM1_WIDTH * WAVERAM1_HEIGHT * 8;
					//printf("Clearing buffer: numBytes: %08X addr: %08X reg50: %08X\n", numBytes, addr, m_zeusbase[0x50]);
				}
				if (logit)
					logerror(" -- Clearing buffer: numBytes: %08X addr: %08X reg51: %08X", numBytes, addr, m_zeusbase[0x51]);
				memset(&m_frameColor[addr], m_fill_color, numBytes);
				memset(&m_frameDepth[addr], m_fill_depth, numBytes/2);
			}
			else if ((m_zeusbase[0x5e] >> 16) != 0xf208 && !(m_zeusbase[0x5e] & 0xffff)) {
			/* If 0x5e==0xf20a0000 (atlantis) or 0xf20d0000 (the grid) then process the read/write now */
				/*
				m_zeusbase[0x5e]:
				bit 0-1: which register triggers write through
				bit 3:   enable write through via these registers
				bit 4:   seems to be set during reads, when 0x51 is used for latching
				bit 5:   unknown, currently used to specify ordering, but this is suspect
				bit 6:   enable autoincrement on write through
				*/
				//if (m_zeusbase[0x50] == 0x00890000)
				if ((m_zeusbase[0x50] == 0x00890000) || (m_zeusbase[0x50] == 0x00e90000))
				{
					frame_write();
				}
				else if (m_zeusbase[0x50] == 0x00720000) {
					/* Do the read */
					frame_read();
				}
				/* make sure we log anything else */
				else if (1 || logit)
					logerror("\tw[50]=%08X [5E]=%08X\n", m_zeusbase[0x50], m_zeusbase[0x5e]);
			}
			break;
		case 0x51:

			/* in this mode, crusnexo expects the reads to immediately latch */
			//if ((m_zeusbase[0x50] == 0x00a20000) || (m_zeusbase[0x50] == 0x00720000))
			if (m_zeusbase[0x50] == 0x00a20000)
				oldval = m_zeusbase[0x51];

			/* this is the address, except in read mode, where it latches values */
			if ((m_zeusbase[0x5e] & 0x10) || (m_zeusbase[0x50] == 0x00a20000))
			{
				m_zeusbase[0x51] = oldval;
				frame_read();
			}
			break;

		case 0x57:
			/* thegrid uses this to write either left or right halves of pixels */
			//if (m_zeusbase[0x50] == 0x00e90000)
			//{
			//  UINT32 addr = frame_addr_from_reg51();
			//  if (m_zeusbase[0x57] & 1)
			//      m_frameColor[addr] = m_zeusbase[0x58];
			//  if (m_zeusbase[0x57] & 4)
			//      m_frameColor[addr+1] = m_zeusbase[0x59];
			//}

			///* make sure we log anything else */
			//else if (logit)
			//  logerror("\t[50]=%08X [5E]=%08X\n", m_zeusbase[0x50], m_zeusbase[0x5e]);
			break;

		case 0x58:
		case 0x59:
		case 0x5a:
			/* if we're in write mode, process it */
			if (m_zeusbase[0x50] == 0x00890000)
			{
				/*
				    m_zeusbase[0x5e]:
				        bit 0-1: which register triggers write through
				        bit 3:   enable write through via these registers
				        bit 4:   seems to be set during reads, when 0x51 is used for latching
				        bit 5:   unknown, currently used to specify ordering, but this is suspect
				        bit 6:   enable autoincrement on write through
				*/
				if ((m_zeusbase[0x5e] & 0x08) && (offset & 3) == (m_zeusbase[0x5e] & 3))
				{
					frame_write();
				}
			}

			/* make sure we log anything else */
			else if ((m_zeusbase[0x5e] >> 16) == 0xf208)
				if (logit)
					logerror("\t[50]=%08X [5E]=%08X", m_zeusbase[0x50], m_zeusbase[0x5e]);
			break;

		case 0x63: case 0x6a: case 0x6b:
		case 0x76: case 0x77:
			if (logit)
				logerror("\tfloatIEEE754 = %8.2f", reinterpret_cast<float&>(m_zeusbase[offset]));
			break;

	}
	if (logit)
		logerror("\n");
}



/*************************************
 *
 *  Process the FIFO
 *
 *************************************/

void zeus2_device::zeus2_pointer_write(UINT8 which, UINT32 value, int logit)
{
#if TRACK_REG_USAGE
subregwrite_count[which]++;
if (subregdata_count[which] < 256)
{
	reg_info **tailptr;

	for (tailptr = &subregdata[which]; *tailptr != nullptr; tailptr = &(*tailptr)->next)
		if ((*tailptr)->value == value)
			break;
	if (*tailptr == nullptr)
	{
		*tailptr = alloc_or_die(reg_info);
		(*tailptr)->next = nullptr;
		(*tailptr)->value = value;
		subregdata_count[which]++;
	}
}
#endif
	if (which<0x40)
		m_renderRegs[which] = value;

	switch (which)
	{
		case 0x40:
			m_renderMode = value;
			// 0x020202 crusnexo quad ??
			//zeus_quad_size = ((m_renderMode & 0xff) == 0x04) ? 14 : 10;
			//zeus_quad_size = (m_renderMode & 0x020000) ? 14 : 10;
			if (logit)
				logerror("\tRender Mode = %06X", m_renderMode);
			//printf("\tRender Mode = %06X\n", m_renderMode);
			break;

		case 0xff:
			// Reset???
			if (logit)
				logerror("\tRender Reset");
			break;

		case 0x04:
			if (logit)
				logerror("\t(R%02X) = %06x Render Loc", which, value);
			break;

		case 0x05:
			zeus_texbase = value % (WAVERAM0_HEIGHT * WAVERAM0_WIDTH);
			if (logit)
				logerror("\t(R%02X)  texbase = %06x", which, zeus_texbase);
			break;

		default:
			if (logit)
				logerror("\t(R%02X) = %06x", which, value);
			break;


#if 0
		case 0x0c:
		case 0x0d:
			// These seem to have something to do with blending.
			// There are fairly unique 0x0C,0x0D pairs for various things:
			// Car reflection on initial screen: 0x40, 0x00
			// Additively-blended "flares": 0xFA, 0xFF
			// Car windshields (and drivers, apparently): 0x82, 0x7D
			// Other minor things: 0xA4, 0x100
			break;
#endif
	}
}

/*************************************
 *  Process the FIFO
 *************************************/

int zeus2_device::zeus2_fifo_process(const UINT32 *data, int numwords)
{
	int dataoffs = 0;

	/* handle logging */
	switch (data[0] >> 24)
	{
		// 0x00: write 32-bit value to low registers
		case 0x00:
			// Ignore the all zeros commmand
			if (((data[0] >> 16) & 0x7f) == 0x0)
				return TRUE;
			// Drop through to 0x05 command
		/* 0x05: write 32-bit value to low registers */
		case 0x05:
			if (numwords < 2)
				return FALSE;
			if (log_fifo)
				log_fifo_command(data, numwords, " -- reg32");
			if (((data[0] >> 16) & 0x7f) != 0x08)
				zeus2_register32_w((data[0] >> 16) & 0x7f, data[1], log_fifo);
			break;

		/* 0x08: set matrix and point (thegrid) */
		case 0x08:
			if (numwords < 14)
				return FALSE;
			dataoffs = 1;

		/* 0x07: set matrix and point (crusnexo) */
		case 0x07:
			if (numwords < 13)
				return FALSE;

			/* extract the matrix from the raw data */
			zeus_matrix[0][0] = convert_float(data[dataoffs + 1]);
			zeus_matrix[0][1] = convert_float(data[dataoffs + 2]);
			zeus_matrix[0][2] = convert_float(data[dataoffs + 3]);
			zeus_matrix[1][0] = convert_float(data[dataoffs + 4]);
			zeus_matrix[1][1] = convert_float(data[dataoffs + 5]);
			zeus_matrix[1][2] = convert_float(data[dataoffs + 6]);
			zeus_matrix[2][0] = convert_float(data[dataoffs + 7]);
			zeus_matrix[2][1] = convert_float(data[dataoffs + 8]);
			zeus_matrix[2][2] = convert_float(data[dataoffs + 9]);

			/* extract the translation point from the raw data */
			zeus_point[0] = convert_float(data[dataoffs + 10]);
			zeus_point[1] = convert_float(data[dataoffs + 11]);
			zeus_point[2] = convert_float(data[dataoffs + 12]);

			if (log_fifo)
			{
				log_fifo_command(data, numwords, "\n");
				logerror("\t\tmatrix ( %8.2f %8.2f %8.2f ) ( %8.2f %8.2f %8.2f ) ( %8.2f %8.2f %8.2f )\n\t\tvector %8.2f %8.2f %8.5f\n",
						(double) zeus_matrix[0][0], (double) zeus_matrix[0][1], (double) zeus_matrix[0][2],
						(double) zeus_matrix[1][0], (double) zeus_matrix[1][1], (double) zeus_matrix[1][2],
						(double) zeus_matrix[2][0], (double) zeus_matrix[2][1], (double) zeus_matrix[2][2],
						(double) zeus_point[0],
						(double) zeus_point[1],
						(double) zeus_point[2]);
			}
			break;

		// 0x14: ?? atlantis
		/* 0x15: set point only (thegrid) */
		/* 0x16: set point only (crusnexo) */
		case 0x14:
		case 0x15:
		case 0x16:
			if (numwords < 4)
				return FALSE;

			/* extract the translation point from the raw data */
			zeus_point[0] = convert_float(data[1]);
			zeus_point[1] = convert_float(data[2]);
			zeus_point[2] = convert_float(data[3]);

			if (log_fifo)
			{
				log_fifo_command(data, numwords, "\n");
				logerror("\t\tvector %8.2f %8.2f %8.5f\n",
						(double) zeus_point[0],
						(double) zeus_point[1],
						(double) zeus_point[2]);
			}
			break;

		/* 0x1c: */
		// 0x1b: the grid
		case 0x1b:
		case 0x1c:
			if (numwords < 4)
				return FALSE;
			if (log_fifo)
			{
				log_fifo_command(data, numwords, " -- unknown control + happens after clear screen\n");
				logerror("\t\tvector2 %8.2f %8.2f %8.5f\n",
						(double) convert_float(data[1]),
						(double) convert_float(data[2]),
						(double) convert_float(data[3]));

				/* extract the translation point from the raw data */
				zeus_point2[0] = convert_float(data[1]);
				zeus_point2[1] = convert_float(data[2]);
				zeus_point2[2] = convert_float(data[3]);
			}
			break;

		// thegrid ???
		case 0x1d:
			if (numwords < 2)
				return FALSE;
			//zeus_point[2] = convert_float(data[1]);
			if (log_fifo)
			{
				log_fifo_command(data, numwords, " -- unknown\n");
				logerror("\t\tdata %8.5f\n",
					(double)convert_float(data[1]));
			}
			break;

		/* 0x23: render model in waveram (thegrid) */
		/* 0x24: render model in waveram (crusnexo) */
		// 0x17: ??? (atlantis)
		case 0x17:
		case 0x23:
		case 0x24:
			if (numwords < 2)
				return FALSE;
			if (log_fifo)
				log_fifo_command(data, numwords, "");
			//zeus2_draw_model(data[1], data[0] & 0xffff, log_fifo);
			zeus2_draw_model(data[1], data[0] & 0x3fff, log_fifo);
			break;

		// 0x2d; set direct render pixels location (atlantis)
		case 0x2d:
			if (numwords < 2)
				return FALSE;
			if (log_fifo)
				log_fifo_command(data, numwords, "\n");
			m_renderPtr = frame_addr_from_phys_addr(data[1]);
			//zeus2_draw_model(data[1], data[0] & 0xff, log_fifo);
			break;

		/* 0x31: sync pipeline? (thegrid) */
		/* 0x32: sync pipeline? (crusnexo) */
		// 0x25 ?? (atlantis)
		case 0x25:
		case 0x31:
		case 0x32:
			if (log_fifo)
				log_fifo_command(data, numwords, " sync? \n");
			//zeus_quad_size = 10;
			break;

		/* 0x38: direct render quad (crusnexo) */
		// 0x38: ?? (atlantis)
		case 0x38:
			if (data[0] == 0x38000000) {
				if (numwords < 3)
					return FALSE;
				// Direct write to frame buffer
				m_frameColor[m_renderPtr++] = conv_rgb555_to_rgb32((UINT16)data[1]);
				m_frameColor[m_renderPtr++] = conv_rgb555_to_rgb32((UINT16)(data[1] >> 16));
				m_frameColor[m_renderPtr++] = conv_rgb555_to_rgb32((UINT16)data[2]);
				m_frameColor[m_renderPtr++] = conv_rgb555_to_rgb32((UINT16)(data[2] >> 16));
			} else if (numwords < 12)
				return FALSE;
			//print_fifo_command(data, numwords, "\n");
			if (log_fifo)
				log_fifo_command(data, numwords, "\n");
			break;

		/* 0x40: ???? */
		case 0x40:
			if (log_fifo)
				log_fifo_command(data, numwords, "\n");
			break;

		default:
			if (data[0] != 0x2c0)
			{
				printf("Unknown command %08X\n", data[0]);
				if (log_fifo)
					log_fifo_command(data, numwords, "\n");
			}
			break;
	}
	return TRUE;
}

/*************************************
 *  Draw a model in waveram
 *************************************/

void zeus2_device::zeus2_draw_model(UINT32 baseaddr, UINT16 count, int logit)
{
	UINT32 databuffer[32];
	int databufcount = 0;
	int model_done = FALSE;
	UINT32 texdata = 0;

	// need to only set the quad size at the start of model ??
	zeus_quad_size = (m_renderMode & 0x020000) ? 14 : 10;

	if (logit)
		logerror(" -- model @ %08X, len %04X\n", baseaddr, count);

	if (count > 0x1000)
		fatalerror("Extreme count\n");

	while (baseaddr != 0 && !model_done)
	{
		const void *base = waveram0_ptr_from_expanded_addr(baseaddr);
		int curoffs;

		/* reset the objdata address */
		baseaddr = 0;

		/* loop until we run out of data */
		for (curoffs = 0; curoffs <= count; curoffs++)
		{
			int countneeded = 2;
			UINT8 cmd;

			/* accumulate 2 words of data */
			databuffer[databufcount++] = WAVERAM_READ32(base, curoffs * 2 + 0);
			databuffer[databufcount++] = WAVERAM_READ32(base, curoffs * 2 + 1);

			/* if this is enough, process the command */
			cmd = databuffer[0] >> 24;
			if ((cmd == 0x38) || (cmd == 0x2d))
				countneeded = zeus_quad_size;
			if (databufcount == countneeded)
			{
				/* handle logging of the command */
				if (logit)
				{
					//if ((cmd == 0x38) || (cmd == 0x2d))
					//  log_render_info();
					int offs;
					logerror("\t");
					for (offs = 0; offs < databufcount; offs++)
						logerror("%08X ", databuffer[offs]);
					logerror("-- ");
				}

				/* handle the command */
				switch (cmd)
				{
					case 0x21:  /* thegrid */
					case 0x22:  /* crusnexo */
						if (((databuffer[0] >> 16) & 0xff) == 0x9b)
						{
							texdata = databuffer[1];
							if (logit)
								logerror("texdata\n");
						}
						else if (logit)
							logerror("unknown offset\n");
						break;

					case 0x31:  /* thegrid */
						if (logit)
							logerror("sync?\n");
						break;

					case 0x29:  // atlantis
					case 0x35:  /* thegrid */
					case 0x36:  /* crusnexo */
						if (logit)
							logerror("reg32");
						zeus2_register32_w((databuffer[0] >> 16) & 0x7f, databuffer[1], logit);
						break;

					case 0x2d:  // atlantis
						poly->zeus2_draw_quad(databuffer, texdata, logit);
						break;

					case 0x38:  /* crusnexo/thegrid */
						poly->zeus2_draw_quad(databuffer, texdata, logit);
						break;

					default:
						//if (quadsize == 10)
						//{
						//  logerror("Correcting quad size\n");
						//  quadsize = 14;
						//}
						if (logit)
							logerror("unknown model data\n");
						break;
				}

				/* reset the count */
				databufcount = 0;
			}
		}
	}
}

/*************************************
 *  Draw a quad
 *************************************/
void zeus2_renderer::zeus2_draw_quad(const UINT32 *databuffer, UINT32 texdata, int logit)
{
	z2_poly_vertex clipvert[8];
	z2_poly_vertex vert[4];
	//  float uscale, vscale;
	float maxy, maxx;
	//  int val1, val2, texwshift;
	int numverts;
	int i;
	//  INT16 normal[3];
	//  INT32 rotnormal[3];

	if (logit)
		m_state->logerror("quad %d\n", m_state->zeus_quad_size);

	if (machine().input().code_pressed(KEYCODE_Q) && (m_state->m_renderRegs[0x5] != 0x1fdf00)) return;
	if (machine().input().code_pressed(KEYCODE_E) && (m_state->m_renderRegs[0x5] != 0x07f540)) return;
	if (machine().input().code_pressed(KEYCODE_R) && (m_state->m_renderRegs[0x5] != 0x081580)) return;
	if (machine().input().code_pressed(KEYCODE_T) && (m_state->m_renderRegs[0x5] != 0x14db00)) return;
	if (machine().input().code_pressed(KEYCODE_Y) && (m_state->m_renderRegs[0x5] != 0x14d880)) return;
	//if (machine().input().code_pressed(KEYCODE_Q) && (texdata & 0xffff) == 0x119) return;
	//if (machine().input().code_pressed(KEYCODE_E) && (texdata & 0xffff) == 0x01d) return;
	//if (machine().input().code_pressed(KEYCODE_R) && (texdata & 0xffff) == 0x11d) return;
	//if (machine().input().code_pressed(KEYCODE_T) && (texdata & 0xffff) == 0x05d) return;
	//if (machine().input().code_pressed(KEYCODE_Y) && (texdata & 0xffff) == 0x0dd) return;
	//if (machine().input().code_pressed(KEYCODE_U) && (texdata & 0xffff) == 0x119) return;
	//if (machine().input().code_pressed(KEYCODE_I) && (texdata & 0xffff) == 0x119) return;
	//if (machine().input().code_pressed(KEYCODE_O) && (texdata & 0xffff) == 0x119) return;
	//if (machine().input().code_pressed(KEYCODE_L) && (texdata & 0x100)) return;

	/*
	0   38800000
	1   x2 | x1
	2   v1 | u1
	3   y2 | y1
	4   v2 | u2
	5   z2 | z1
	6   v3 | u3
	7   v4 | u4
	8   ???
	9   x4 | x3
	10  y4 | y3
	11  z4 | z3

	In memory:
	+0 = ???
	+1 = set via $05410000/value
	+2 = x1
	+3 = y1
	+4 = z1
	+5 = x2
	+6 = y2
	+7 = z2
	+8 = x3
	+9 = y3
	+10= z3
	+11= x4
	+12= y4
	+13= z4
	+14= uv1
	+15= uv2
	+16= uv3
	+17= uv4
	+18= set via $05200000/$05000000 | (value << 10) (uvoffset?)
	+19= ???


	38810000 00000000 00C7|FF38 FF5E|FF5E 15400154 11400114 00000000 00000000 FF38|00C7 00A3|00A3 -- quad
	xxxx|xxxx yyyy|yyyy                                     xxxx|xxxx yyyy|yyyy
	*/
	// Altantis rendermode: 0x024004 startup, then 0x020202, then 0x021E0E
	/* extract raw x,y,z */
	if (m_state->m_atlantis) {
			// Atlantis quad 14
		texdata = databuffer[1];
		vert[0].x = (INT16)databuffer[2];
		vert[0].y = (INT16)databuffer[3];
		vert[0].p[0] = (INT16)databuffer[4];
		vert[0].p[1] = (databuffer[5] >> 0) & 0xff;
		vert[0].p[2] = (databuffer[5] >> 8) & 0xff;

		vert[1].x = (INT16)(databuffer[2] >> 16);
		vert[1].y = (INT16)(databuffer[3] >> 16);
		vert[1].p[0] = (INT16)(databuffer[4] >> 16);
		vert[1].p[1] = (databuffer[5] >> 16) & 0xff;
		vert[1].p[2] = (databuffer[5] >> 24) & 0xff;

		vert[2].x = (INT16)databuffer[6];
		vert[2].y = (INT16)databuffer[7];
		vert[2].p[0] = (INT16)databuffer[8];
		vert[2].p[1] = (databuffer[9] >> 0) & 0xff;
		vert[2].p[2] = (databuffer[9] >> 8) & 0xff;

		vert[3].x = (INT16)(databuffer[6] >> 16);
		vert[3].y = (INT16)(databuffer[7] >> 16);
		vert[3].p[0] = (INT16)(databuffer[8] >> 16);
		vert[3].p[1] = (databuffer[9] >> 16) & 0xff;
		vert[3].p[2] = (databuffer[9] >> 24) & 0xff;
	}
	else {
		//printf("renderMode: %06X\n", m_state->m_renderMode);
		vert[0].x = (INT16)databuffer[2];
		vert[0].y = (INT16)databuffer[3];
		vert[0].p[0] = (INT16)databuffer[6];
		vert[0].p[1] = (databuffer[1] >> 2) & 0xff;
		vert[0].p[2] = (databuffer[1] >> 18) & 0xff;

		vert[1].x = (INT16)(databuffer[2] >> 16);
		vert[1].y = (INT16)(databuffer[3] >> 16);
		vert[1].p[0] = (INT16)(databuffer[6] >> 16);
		vert[1].p[1] = (databuffer[4] >> 2) & 0xff;
		vert[1].p[2] = (databuffer[4] >> 12) & 0xff;

		vert[2].x = (INT16)databuffer[8];
		vert[2].y = (INT16)databuffer[9];
		vert[2].p[0] = (INT16)databuffer[7];
		vert[2].p[1] = (databuffer[4] >> 22) & 0xff;
		vert[2].p[2] = (databuffer[5] >> 2) & 0xff;

		vert[3].x = (INT16)(databuffer[8] >> 16);
		vert[3].y = (INT16)(databuffer[9] >> 16);
		vert[3].p[0] = (INT16)(databuffer[7] >> 16);
		vert[3].p[1] = (databuffer[5] >> 12) & 0xff;
		vert[3].p[2] = (databuffer[5] >> 22) & 0xff;
	}
	int unknown[8];
	float unknownFloat[4];
	if (m_state->zeus_quad_size == 14) {
		// buffer 10-13 ???? 00000000 1FF7FC00 00000000 1FF7FC00 -- mwskinsa quad 14
		/* 10:13 16 bit coordinates */
		unknown[0] = (INT16)databuffer[10];
		unknown[1] = (INT16)(databuffer[10] >> 16);
		unknown[2] = (INT16)databuffer[11];
		unknown[3] = (INT16)(databuffer[11] >> 16);
		unknown[4] = (INT16)databuffer[12];
		unknown[5] = (INT16)(databuffer[12] >> 16);
		unknown[6] = (INT16)databuffer[13];
		unknown[7] = (INT16)(databuffer[13] >> 16);
		unknownFloat[0] = m_state->convert_float(databuffer[10]);
		unknownFloat[1] = m_state->convert_float(databuffer[11]);
		unknownFloat[2] = m_state->convert_float(databuffer[12]);
		unknownFloat[3] = m_state->convert_float(databuffer[13]);
	}
	/*
	vert[0].x = (INT16)databuffer[1];
	vert[0].y = (INT16)databuffer[3];
	vert[0].p[0] = (INT16)databuffer[5];
	vert[0].p[1] = (UINT16)databuffer[2];
	vert[0].p[2] = (UINT16)(databuffer[2] >> 16);

	vert[1].x = (INT16)(databuffer[1] >> 16);
	vert[1].y = (INT16)(databuffer[3] >> 16);
	vert[1].p[0] = (INT16)(databuffer[5] >> 16);
	vert[1].p[1] = (UINT16)databuffer[4];
	vert[1].p[2] = (UINT16)(databuffer[4] >> 16);

	vert[2].x = (INT16)databuffer[9];
	vert[2].y = (INT16)databuffer[10];
	vert[2].p[0] = (INT16)databuffer[11];
	vert[2].p[1] = (UINT16)databuffer[6];
	vert[2].p[2] = (UINT16)(databuffer[6] >> 16);

	vert[3].x = (INT16)(databuffer[9] >> 16);
	vert[3].y = (INT16)(databuffer[10] >> 16);
	vert[3].p[0] = (INT16)(databuffer[11] >> 16);
	vert[3].p[1] = (UINT16)databuffer[7];
	vert[3].p[2] = (UINT16)(databuffer[7] >> 16);
	*/

	int logextra = 1;

	//float xScale = 1.0f / 8.0f;
	//float yScale = 1.0f / 8.0f;
	//float zScale = 1.0f / 8.0f;
	for (i = 0; i < 4; i++)
	{
		float x = vert[i].x;
		float y = vert[i].y;
		float z = vert[i].p[0];
		//if (0) {
		//  x *= xScale;
		//  y *= yScale;
		//  z *= zScale;

		//}
		vert[i].x = x * m_state->zeus_matrix[0][0] + y * m_state->zeus_matrix[0][1] + z * m_state->zeus_matrix[0][2];
		vert[i].y = x * m_state->zeus_matrix[1][0] + y * m_state->zeus_matrix[1][1] + z * m_state->zeus_matrix[1][2];
		vert[i].p[0] = x * m_state->zeus_matrix[2][0] + y * m_state->zeus_matrix[2][1] + z * m_state->zeus_matrix[2][2];

		if (1 || !(m_state->m_renderRegs[0x14] & 0x000001)) {
			vert[i].x += m_state->zeus_point[0];
			vert[i].y += m_state->zeus_point[1];
			vert[i].p[0] += m_state->zeus_point[2];
		}
		if (0)
			vert[i].p[0] += m_state->zbase;
		else {
			int shift;
			// Not sure why but atlantis coordinates are scaled differently
			if (0 && m_state->m_atlantis)
				shift = 2048 >> m_state->m_zeusbase[0x6c];
			else
				shift = 1024 >> m_state->m_zeusbase[0x6c];
			//int shift = 8 << ((m_state->m_renderRegs[0x14] >> 4) & 0xf);
			vert[i].p[0] += shift;
		}
		//vert[i].p[0] *= 2.0f;
		vert[i].p[2] += texdata >> 16;
		vert[i].p[1] *= 256.0f;
		vert[i].p[2] *= 256.0f;

		// back face cull using polygon normal and first vertex
		if (1 && i == 0)
		{
			INT8 normal[3];
			float rotnormal[3];

			normal[0] = databuffer[0] >> 0;
			normal[1] = databuffer[0] >> 8;
			normal[2] = databuffer[0] >> 16;

			rotnormal[0] = normal[0] * m_state->zeus_matrix[0][0] + normal[1] * m_state->zeus_matrix[0][1] + normal[2] * m_state->zeus_matrix[0][2];
			rotnormal[1] = normal[0] * m_state->zeus_matrix[1][0] + normal[1] * m_state->zeus_matrix[1][1] + normal[2] * m_state->zeus_matrix[1][2];
			rotnormal[2] = normal[0] * m_state->zeus_matrix[2][0] + normal[1] * m_state->zeus_matrix[2][1] + normal[2] * m_state->zeus_matrix[2][2];

			float dot = rotnormal[0] * vert[0].x + rotnormal[1] * vert[0].y + rotnormal[2] * vert[0].p[0];

			if (dot >= 0)
				return;
		}


		if (logextra & logit)
		{
			m_state->logerror("\t\t(%f,%f,%f) (%02X,%02X)\n",
				(double)vert[i].x, (double)vert[i].y, (double)vert[i].p[0],
				(int)(vert[i].p[1] / 256.0f), (int)(vert[i].p[2] / 256.0f));
		}
	}
	if (logextra & logit && m_state->zeus_quad_size == 14) {
		m_state->logerror("uknown: int16: %d %d %d %d %d %d %d %d float: %f %f %f %f\n",
			unknown[0], unknown[1], unknown[2], unknown[3], unknown[4], unknown[5], unknown[6], unknown[7],
			unknownFloat[0], unknownFloat[1], unknownFloat[2], unknownFloat[3]);
	}
	bool enable_perspective = true;// !(m_state->m_renderRegs[0x14] & 0x80);
	float clipVal = enable_perspective ? 1.0f / 512.0f / 4.0f : -1.0f;
	numverts = this->zclip_if_less(4, &vert[0], &clipvert[0], 4, clipVal);
	if (numverts < 3)
		return;

	float xOrigin = reinterpret_cast<float&>(m_state->m_zeusbase[0x6a]);
	float yOrigin = reinterpret_cast<float&>(m_state->m_zeusbase[0x6b]);

	float oozBase = (m_state->m_atlantis) ? 1024.0f : 512.0f;

	maxx = maxy = -1000.0f;
	for (i = 0; i < numverts; i++)
	{
		// mwskinsa has R14=0x40a1 for tips box which has z=0
		//if (!(m_state->m_renderRegs[0x14] & 0x1)) {
		if (enable_perspective) {
			// 412.0f here works for crusnexo
			// 1024.0f works for mwskinsa
			float ooz = oozBase / clipvert[i].p[0];
			//float ooz = 1024.0f / clipvert[i].p[0];
			//float ooz = float(1 << m_state->m_zeusbase[0x6c]) / clipvert[i].p[0];

			clipvert[i].x *= ooz;
			clipvert[i].y *= ooz;
		}
		if (1) {
			//clipvert[i].x += 256.5f / 1.0f;
			//clipvert[i].y += 200.5f / 1.0f;
			clipvert[i].x += xOrigin;
			clipvert[i].y += yOrigin;
		}

		clipvert[i].p[0] *= 65536.0f * 16.0f;

		maxx = std::max(maxx, clipvert[i].x);
		maxy = std::max(maxy, clipvert[i].y);
		if (logextra & logit)
			m_state->logerror("\t\t\tTranslated=(%f,%f)\n", (double)clipvert[i].x, (double)clipvert[i].y);
	}
	for (i = 0; i < numverts; i++)
	{
		if (clipvert[i].x == maxx)
			clipvert[i].x += 0.0005f;
		if (clipvert[i].y == maxy)
			clipvert[i].y += 0.0005f;
	}

	zeus2_poly_extra_data& extra = this->object_data_alloc();
	int texmode = texdata & 0xffff;
	// 0x014d == atlantis initial screen and scoreboard background
	//if (texmode != 0x014D) return;
	// Just a guess but seems to work
	//extra.texwidth = 2 << ((texmode >> 2) & 7);
	extra.texwidth = 0x20 << ((texmode >> 2) & 3);
	//extra.texwidth = 0x2 << ((texmode >> 2) & 0xf);

	//switch (texmode)
	//{
	//case 0x14d:     // atlantis
	//case 0x18e:     // atlantis
	//case 0x01d:     /* crusnexo: RHS of score bar */
	//case 0x05d:     /* crusnexo: background, road */
	//case 0x0dd:     /* crusnexo: license plate letters */
	//case 0x11d:     /* crusnexo: LHS of score bar */
	//case 0x15d:     /* crusnexo */
	//case 0x85d:     /* crusnexo */
	//case 0x95d:     /* crusnexo */
	//case 0xc1d:     /* crusnexo */
	//case 0xc5d:     /* crusnexo */
	//  extra.texwidth = 256;
	//  break;

	//case 0x18a:     // atlantis
	//case 0x059:     /* crusnexo */
	//case 0x0d9:     /* crusnexo */
	//case 0x119:     /* crusnexo: license plates */
	//case 0x159:     /* crusnexo */
	//  extra.texwidth = 128;
	//  break;

	//case 0x055:     /* crusnexo */
	//case 0x145:     // atlantis
	//case 0x155:     /* crusnexo */
	//  extra.texwidth = 64;
	//  break;

	//case 0x000:     // thegrid guess
	//case 0x120:     // thegrid guess
	//case 0x140:     // atlantis
	//case 0x141:     // atlantis
	//  extra.texwidth = 32;
	//  break;

	//default:
	//{
	//  static UINT8 hits[0x10000];
	//  if (!hits[(texdata & 0xffff)])
	//  {
	//      hits[(texdata & 0xffff)] = 1;
	//      printf("texMode = %04X\n", (texdata & 0xffff));
	//  }
	//  break;
	//}
	//}

	extra.solidcolor = 0;//m_zeusbase[0x00] & 0x7fff;
	extra.zoffset = m_state->m_renderRegs[0x15];
	extra.alpha = 0;//m_zeusbase[0x4e];
	extra.transcolor = 0x100; // !(texmode & 100) ? 0 : 0x100;
	extra.texbase = WAVERAM_BLOCK0_EXT(m_state->zeus_texbase);
	extra.palbase = m_state->waveram0_ptr_from_expanded_addr(m_state->m_zeusbase[0x41]);
	//extra.depth_test_enable = !(m_state->m_renderMode & 0x020000);
	// crusnexo text is R14=0x4062
	extra.depth_test_enable = !(m_state->m_renderRegs[0x14] & 0x000020);
	//extra.depth_test_enable = !(m_state->m_renderMode & 0x000002);
	//extra.depth_test_enable = true; // (texmode & 0x0010);
	extra.depth_write_enable = true;

	// Note: Before being converted to the "poly.h" interface, this used to call the polylgcy function
	//       poly_render_quad_fan.  The behavior seems to be the same as it once was after a few short
	//       tests, but the (numverts == 5) statement below may actually be a quad fan instead of a 5-sided
	//       polygon.
	if (numverts == 3)
		render_triangle(m_state->zeus_cliprect, render_delegate(FUNC(zeus2_renderer::render_poly_8bit), this), 4, clipvert[0], clipvert[1], clipvert[2]);
	else if (numverts == 4)
		render_polygon<4>(m_state->zeus_cliprect, render_delegate(FUNC(zeus2_renderer::render_poly_8bit), this), 4, clipvert);
	else if (numverts == 5)
		render_polygon<5>(m_state->zeus_cliprect, render_delegate(FUNC(zeus2_renderer::render_poly_8bit), this), 4, clipvert);
}



/*************************************
*  Rasterizers
*************************************/

void zeus2_renderer::render_poly_8bit(INT32 scanline, const extent_t& extent, const zeus2_poly_extra_data& object, int threadid)
{
	INT32 curz = extent.param[0].start;
	INT32 curu = extent.param[1].start;
	INT32 curv = extent.param[2].start;
	//  INT32 curi = extent.param[3].start;
	INT32 dzdx = extent.param[0].dpdx;
	INT32 dudx = extent.param[1].dpdx;
	INT32 dvdx = extent.param[2].dpdx;
	//  INT32 didx = extent.param[3].dpdx;
	const void *texbase = object.texbase;
	const void *palbase = object.palbase;
	UINT16 transcolor = object.transcolor;
	int texwidth = object.texwidth;
	int x;

	UINT32 addr = m_state->frame_addr_from_xy(0, scanline, true);
	UINT16 *depthptr = &m_state->m_frameDepth[addr];
	UINT32 *colorptr = &m_state->m_frameColor[addr];
	for (x = extent.startx; x < extent.stopx; x++)
	{
		bool depth_pass = true;
		if (object.depth_test_enable) {
			//UINT16 *depthptr = WAVERAM_PTRDEPTH(m_state->zeus_renderbase, scanline, x);
			INT32 depth = (curz >> 16) + object.zoffset;
			//if (depth > 0x7fff) depth = 0x7fff;
			if (depth > 0xffff) depth = 0xffff;
			if (depth < 0 || depth > depthptr[x])
				depth_pass = false;
			else if (object.depth_write_enable)
				depthptr[x] = depth;
		}
		if (depth_pass) {
			int u0 = (curu >> 8);// & (texwidth - 1);
			int v0 = (curv >> 8);// & 255;
			int u1 = (u0 + 1);
			int v1 = (v0 + 1);
			UINT8 texel0 = m_state->get_texel_8bit(texbase, v0, u0, texwidth);
			if (texel0 == object.transcolor)
				continue;
			UINT8 texel1 = m_state->get_texel_8bit(texbase, v0, u1, texwidth);
			UINT8 texel2 = m_state->get_texel_8bit(texbase, v1, u0, texwidth);
			UINT8 texel3 = m_state->get_texel_8bit(texbase, v1, u1, texwidth);
			if (texel0 != transcolor)
			{
				UINT32 color0 = WAVERAM_READ16(palbase, texel0);
				UINT32 color1 = WAVERAM_READ16(palbase, texel1);
				UINT32 color2 = WAVERAM_READ16(palbase, texel2);
				UINT32 color3 = WAVERAM_READ16(palbase, texel3);
				color0 = ((color0 & 0x7c00) << 9) | ((color0 & 0x3e0) << 6) | ((color0 & 0x1f) << 3);
				color1 = ((color1 & 0x7c00) << 9) | ((color1 & 0x3e0) << 6) | ((color1 & 0x1f) << 3);
				color2 = ((color2 & 0x7c00) << 9) | ((color2 & 0x3e0) << 6) | ((color2 & 0x1f) << 3);
				color3 = ((color3 & 0x7c00) << 9) | ((color3 & 0x3e0) << 6) | ((color3 & 0x1f) << 3);
				rgb_t filtered = rgbaint_t::bilinear_filter(color0, color1, color2, color3, curu, curv);
				//WAVERAM_WRITEPIX(m_state->zeus_renderbase, scanline, x, filtered);
				//*depthptr = depth;
				colorptr[x] = filtered;
			}
		}
		curz += dzdx;
		curu += dudx;
		curv += dvdx;
		//      curi += didx;
	}
}

/*************************************
 *  Debugging tools
 *************************************/

void zeus2_device::log_fifo_command(const UINT32 *data, int numwords, const char *suffix)
{
	int wordnum;
	logerror("Zeus cmd %02X :", data[0] >> 24);
	for (wordnum = 0; wordnum < numwords; wordnum++)
		logerror(" %08X", data[wordnum]);
	logerror("%s", suffix);
}

void zeus2_device::print_fifo_command(const UINT32 *data, int numwords, const char *suffix)
{
	int wordnum;
	printf("Zeus cmd %02X :", data[0] >> 24);
	for (wordnum = 0; wordnum < numwords; wordnum++)
		printf(" %08X", data[wordnum]);
	printf("%s", suffix);
}

void zeus2_device::log_render_info()
{
	logerror("-- RMode = %08X", m_renderMode);
	for (int i = 1; i <= 0x15; ++i)
		logerror(" R%02X=%06X", i, m_renderRegs[i]);
	logerror("\n-- RMode ");
	for (int i = 0x63; i <= 0x6f; ++i)
		logerror(" %02X=%08X", i, m_zeusbase[i]);
	logerror("\n");
}
