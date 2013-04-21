/***********************************************************************************************************


 NES/Famicom cartridge emulation for Somari Team PCBs

 Copyright MESS Team.
 Visit http://mamedev.org for licensing and usage restrictions.


 Here we emulate the Somari Team PCBs [mapper 116]


 ***********************************************************************************************************/


#include "emu.h"
#include "machine/nes_somari.h"


#ifdef NES_PCB_DEBUG
#define VERBOSE 1
#else
#define VERBOSE 0
#endif

#define LOG_MMC(x) do { if (VERBOSE) logerror x; } while (0)


//-------------------------------------------------
//  constructor
//-------------------------------------------------

const device_type NES_SOMARI = &device_creator<nes_somari_device>;


nes_somari_device::nes_somari_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
					: nes_txrom_device(mconfig, NES_SOMARI, "NES Cart Team Somari PCB", tag, owner, clock, "nes_somari", __FILE__)
{
}



void nes_somari_device::device_start()
{
	common_start();
	save_item(NAME(m_board_mode));

	// MMC3
	save_item(NAME(m_mmc_prg_bank));
	save_item(NAME(m_mmc_vrom_bank));
	save_item(NAME(m_latch));
	save_item(NAME(m_prg_base));
	save_item(NAME(m_prg_mask));
	save_item(NAME(m_chr_base));
	save_item(NAME(m_chr_mask));

	save_item(NAME(m_irq_enable));
	save_item(NAME(m_irq_count));
	save_item(NAME(m_irq_count_latch));
	save_item(NAME(m_irq_clear));

	// MMC1
	save_item(NAME(m_count));
	save_item(NAME(m_mmc1_latch));
	save_item(NAME(m_mmc1_reg));

	// VRC2
	save_item(NAME(m_vrc_prg_bank));
	save_item(NAME(m_vrc_vrom_bank));
}

void nes_somari_device::pcb_reset()
{
	m_chr_source = m_vrom_chunks ? CHRROM : CHRRAM;
	prg32(0);
	chr8(0, m_chr_source);

	m_board_mode = 2; // mode

	// MMC3
	m_prg_base = m_chr_base = 0;
	m_prg_mask = 0xff;
	m_chr_mask = 0xff;
	m_latch = 0;
	m_mmc_prg_bank[0] = 0x3c;
	m_mmc_prg_bank[1] = 0x3d;
	m_mmc_prg_bank[2] = 0xfe;
	m_mmc_prg_bank[3] = 0xff;
	m_mmc_vrom_bank[0] = 0x00;
	m_mmc_vrom_bank[1] = 0x01;
	m_mmc_vrom_bank[2] = 0x04;
	m_mmc_vrom_bank[3] = 0x05;
	m_mmc_vrom_bank[4] = 0x06;
	m_mmc_vrom_bank[5] = 0x07;

	m_alt_irq = 0;
	m_irq_enable = 0;
	m_irq_count = m_irq_count_latch = 0;
	m_irq_clear = 0;

	// MMC1 regs
	m_count = 0;
	m_mmc1_latch = 0;
	m_mmc1_reg[0] = 0x0c;
	m_mmc1_reg[1] = 0x00;
	m_mmc1_reg[2] = 0x00;
	m_mmc1_reg[3] = 0x00;

	// VRC2 regs
	m_vrc_prg_bank[0] = 0x00;
	m_vrc_prg_bank[1] = 0x01;
	for (int i = 0; i < 8; ++i)
		m_vrc_vrom_bank[i] = i;
	bank_update_switchmode();
}




/*-------------------------------------------------
 mapper specific handlers
 -------------------------------------------------*/

/*-------------------------------------------------

 SOMERI TEAM

 iNES: mapper 116


 Emulation note about regs in MESS: currently,
 - m_mmc_prg_bank[n] for n=0,...,3 represent the MMC3 PRG banks (inherited from base class)
 - m_mmc_vrom_bank[n] for n=0,...,5 represent the MMC3 CHR banks (inherited from base class)

 - m_mmc1_reg[n] for n=0,1,2,3 represent the MMC1 regs
 - m_count and m_mmc1_latch are additional variables for MMC1 (notice that MMC3 uses a diff m_latch!)

 - m_vrc_prg_bank[n] for n=0,1 represent the VRC2 PRG banks
 - m_vrc_vrom_bank[n] for n=0,...,7 represent the VRC2 CHR banks


 In MESS: Preliminary support

 -------------------------------------------------*/

// MMC1 Mode emulation
void nes_somari_device::mmc1_set_prg()
{
	UINT8 prg_mode = m_mmc1_reg[0] & 0x0c;
	UINT8 prg_offset = m_mmc1_reg[1] & 0x10;

	switch (prg_mode)
	{
		case 0x00:
		case 0x04:
			prg32((prg_offset + m_mmc1_reg[3]) >> 1);
			break;
		case 0x08:
			prg16_89ab(prg_offset + 0);
			prg16_cdef(prg_offset + m_mmc1_reg[3]);
			break;
		case 0x0c:
			prg16_89ab(prg_offset + m_mmc1_reg[3]);
			prg16_cdef(prg_offset + 0x0f);
			break;
	}
}

void nes_somari_device::mmc1_set_chr()
{
	UINT8 chr_mode = BIT(m_mmc1_reg[0], 4);

	if (chr_mode)
	{
		chr4_0(m_mmc1_reg[1] & 0x1f, m_chr_source);
		chr4_4(m_mmc1_reg[2] & 0x1f, m_chr_source);
	}
	else
		chr8((m_mmc1_reg[1] & 0x1f) >> 1, m_chr_source);
}

WRITE8_MEMBER(nes_somari_device::mmc1_w)
{
	assert(m_board_mode == 2);

	if (data & 0x80)
	{
		m_count = 0;
		m_mmc1_latch = 0;

		m_mmc1_reg[0] |= 0x0c;
		mmc1_set_prg();
		return;
	}

	if (m_count < 5)
	{
		if (m_count == 0) m_mmc1_latch = 0;
		m_mmc1_latch >>= 1;
		m_mmc1_latch |= (data & 0x01) ? 0x10 : 0x00;
		m_count++;
	}

	if (m_count == 5)
	{
		switch (offset & 0x6000)
		{
			case 0x0000:
				m_mmc1_reg[0] = m_mmc1_latch;
				switch (m_mmc1_reg[0] & 0x03)
				{
					case 0: set_nt_mirroring(PPU_MIRROR_LOW); break;
					case 1: set_nt_mirroring(PPU_MIRROR_HIGH); break;
					case 2: set_nt_mirroring(PPU_MIRROR_VERT); break;
					case 3: set_nt_mirroring(PPU_MIRROR_HORZ); break;
				}
				mmc1_set_chr();
				mmc1_set_prg();
				break;
			case 0x2000:
				m_mmc1_reg[1] = m_mmc1_latch;
				mmc1_set_chr();
				mmc1_set_prg();
				break;
			case 0x4000:
				m_mmc1_reg[2] = m_mmc1_latch;
				mmc1_set_chr();
				break;
			case 0x6000:
				m_mmc1_reg[3] = m_mmc1_latch;
				mmc1_set_prg();
				break;
		}

		m_count = 0;
	}
}

// MMC3 Mode emulation
WRITE8_MEMBER(nes_somari_device::mmc3_w)
{
	UINT8 mmc_helper, cmd;

	assert(m_board_mode == 1);

	switch (offset & 0x6001)
	{
		case 0x0000:
			mmc_helper = m_latch ^ data;
			m_latch = data;

			if (mmc_helper & 0x40)
				set_prg(m_prg_base, m_prg_mask);

			if (mmc_helper & 0x80)
				set_chr(m_chr_source, m_chr_base, m_chr_mask);
			break;

		case 0x0001:
			cmd = m_latch & 0x07;
			switch (cmd)
			{
				case 0: case 1:
				case 2: case 3: case 4: case 5:
					m_mmc_vrom_bank[cmd] = data;
					set_chr(m_chr_source, m_chr_base, m_chr_mask);
					break;
				case 6:
				case 7:
					m_mmc_prg_bank[cmd - 6] = data;
					set_prg(m_prg_base, m_prg_mask);
					break;
			}
			break;

		case 0x2000:
			set_nt_mirroring(BIT(data, 0) ? PPU_MIRROR_HORZ : PPU_MIRROR_VERT);
			break;
		case 0x2001: break;
		case 0x4000: m_irq_count_latch = data; break;
		case 0x4001: m_irq_count = 0; break;
		case 0x6000: m_irq_enable = 0; break;
		case 0x6001: m_irq_enable = 1; break;
	}
}

// VRC2 Mode emulation
WRITE8_MEMBER(nes_somari_device::vrc2_w)
{
	UINT8 bank, shift;

	assert(m_board_mode == 0);

	switch (offset & 0x7000)
	{
		case 0x0000:
			m_vrc_prg_bank[0] = data;
			prg8_89(m_vrc_prg_bank[0]);
			break;

		case 0x1000:
			switch (data & 0x03)
			{
				case 0x00: set_nt_mirroring(PPU_MIRROR_VERT); break;
				case 0x01: set_nt_mirroring(PPU_MIRROR_HORZ); break;
				case 0x02: set_nt_mirroring(PPU_MIRROR_LOW); break;
				case 0x03: set_nt_mirroring(PPU_MIRROR_HIGH); break;
			}
			break;

		case 0x2000:
			m_vrc_prg_bank[1] = data;
			prg8_ab(m_vrc_prg_bank[1]);
			break;

		case 0x3000:
		case 0x4000:
		case 0x5000:
		case 0x6000:
			bank = ((offset & 0x7000) - 0x3000) / 0x0800 + BIT(offset, 1);
			shift = BIT(offset, 2) * 4;
			data = (data & 0x0f) << shift;
			m_vrc_vrom_bank[bank] = data | m_chr_base;
			chr1_x(bank, m_vrc_vrom_bank[bank], CHRROM);
			break;
	}
}

WRITE8_MEMBER(nes_somari_device::write_h)
{
	LOG_MMC(("somari write_h, mode %d, offset: %04x, data: %02x\n", m_board_mode, offset, data));

	switch (m_board_mode)
	{
		case 0x00: vrc2_w(space, offset, data, mem_mask); break;
		case 0x01: mmc3_w(space, offset, data, mem_mask); break;
		case 0x02: mmc1_w(space, offset, data, mem_mask); break;
	}
}

void nes_somari_device::bank_update_switchmode()
{
	switch (m_board_mode)
	{
		case 0x00:
			prg8_89(m_vrc_prg_bank[0]);
			prg8_ab(m_vrc_prg_bank[1]);
			for (int i = 0; i < 8; i++)
				chr1_x(i, m_vrc_vrom_bank[i], CHRROM);
			break;
		case 0x01:
			set_prg(m_prg_base, m_prg_mask);
			set_chr(m_chr_source, m_chr_base, m_chr_mask);
			break;
		case 0x02:
			mmc1_set_prg();
			mmc1_set_chr();
			break;
	}
}

WRITE8_MEMBER(nes_somari_device::write_l)
{
	LOG_MMC(("somari write_l, offset: %04x, data: %02x\n", offset, data));
	offset += 0x100;

	if (offset & 0x100)
	{
		m_board_mode = data & 0x03;
		m_chr_base = ((m_board_mode & 0x04) << 6);
		if (m_board_mode != 1)
			m_irq_enable = 0;
		bank_update_switchmode();
	}
}
