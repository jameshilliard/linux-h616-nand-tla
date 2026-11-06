/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013 Allwinnertech, kevin.z.m <kevin@allwinnertech.com>
 * Unchanged bodies shared by the four pinned BSP variants in source.json.
 */
__s32 NFC_SetRandomSeed(__u32 random_seed)
{
	__u32 cfg;


	  cfg = NFC_READ_REG(NFC_REG_ECC_CTL);
	  cfg &= 0x0000ffff;
	  cfg |= (random_seed<<16);
	  NFC_WRITE_REG(NFC_REG_ECC_CTL,cfg);

	return 0;
}

__s32 NFC_RandomEnable(void)
{
	__u32 cfg;


	cfg = NFC_READ_REG(NFC_REG_ECC_CTL);
	cfg |= (0x1<<9);
	NFC_WRITE_REG(NFC_REG_ECC_CTL,cfg);


	return 0;
}

__s32 NFC_RandomDisable(void)
{
	__u32 cfg;


	cfg = NFC_READ_REG(NFC_REG_ECC_CTL);
	cfg &= (~(0x1<<9));
	NFC_WRITE_REG(NFC_REG_ECC_CTL,cfg);


	return 0;
}
