/*
 * MFD core driver for the X-Powers' Power Management ICs
 *
 * AXP20x typically comprises an adaptive USB-Compatible PWM charger, BUCK DC-DC
 * converters, LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as configurable GPIOs.
 *
 * This file contains the interface independent core functions.
 *
 * Copyright (C) 2014 Carlo Caione
 *
 * Author: Carlo Caione <carlo@caione.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/axp20x.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/delay.h>

#define AXP20X_OFF	0x80

#define AXP806_REG_ADDR_EXT_ADDR_MASTER_MODE	0
#define AXP806_REG_ADDR_EXT_ADDR_SLAVE_MODE	BIT(4)

static const char * const axp20x_model_names[] = {
	"AXP152",
	"AXP202",
	"AXP209",
	"AXP221",
	"AXP223",
	"AXP288",
	"AXP803",
	"AXP806",
	"AXP809",
	"AXP813",
};

static const struct regmap_range axp152_writeable_ranges[] = {
	regmap_reg_range(AXP152_LDO3456_DC1234_CTRL, AXP152_IRQ3_STATE),
	regmap_reg_range(AXP152_DCDC_MODE, AXP152_PWM1_DUTY_CYCLE),
};

static const struct regmap_range axp152_volatile_ranges[] = {
	regmap_reg_range(AXP152_PWR_OP_MODE, AXP152_PWR_OP_MODE),
	regmap_reg_range(AXP152_IRQ1_EN, AXP152_IRQ3_STATE),
	regmap_reg_range(AXP152_GPIO_INPUT, AXP152_GPIO_INPUT),
};

static const struct regmap_access_table axp152_writeable_table = {
	.yes_ranges	= axp152_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp152_writeable_ranges),
};

static const struct regmap_access_table axp152_volatile_table = {
	.yes_ranges	= axp152_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp152_volatile_ranges),
};

static const struct regmap_range axp20x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP20X_CHRG_CTRL2),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP20X_FG_RES),
	regmap_reg_range(AXP20X_RDC_H, AXP20X_OCV(AXP20X_OCV_MAX)),
};

static const struct regmap_range axp20x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP20X_USB_OTG_STATUS),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP20X_CHRG_CTRL2),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_ACIN_V_ADC_H, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_GPIO20_SS, AXP20X_GPIO3_CTRL),
	regmap_reg_range(AXP20X_CHRG_CC_31_24, AXP20X_DISCHRG_CC_7_0),
	regmap_reg_range(AXP20X_FG_RES, AXP20X_RDC_L),
};

static const struct regmap_access_table axp20x_writeable_table = {
	.yes_ranges	= axp20x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_writeable_ranges),
};

static const struct regmap_access_table axp20x_volatile_table = {
	.yes_ranges	= axp20x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_volatile_ranges),
};

/* AXP22x ranges are shared with the AXP809, as they cover the same range */
static const struct regmap_range axp22x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_CHRG_CTRL1, AXP22X_CHRG_CTRL3),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP22X_BATLOW_THRES1),
};

static const struct regmap_range axp22x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP20X_PWR_OP_MODE),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP22X_GPIO_STATE, AXP22X_GPIO_STATE),
	regmap_reg_range(AXP22X_PMIC_TEMP_H, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_FG_RES, AXP20X_FG_RES),
};

static const struct regmap_access_table axp22x_writeable_table = {
	.yes_ranges	= axp22x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_writeable_ranges),
};

static const struct regmap_access_table axp22x_volatile_table = {
	.yes_ranges	= axp22x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp22x_volatile_ranges),
};

/* AXP288 ranges are shared with the AXP803, as they cover the same range */
static const struct regmap_range axp288_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ6_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP288_FG_TUNE5),
};

static const struct regmap_range axp288_volatile_ranges[] = {
	regmap_reg_range(AXP20X_PWR_INPUT_STATUS, AXP288_POWER_REASON),
	regmap_reg_range(AXP288_BC_GLOBAL, AXP288_BC_GLOBAL),
	regmap_reg_range(AXP288_BC_DET_STAT, AXP288_BC_DET_STAT),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IPSOUT_V_HIGH_L),
	regmap_reg_range(AXP20X_TIMER_CTRL, AXP20X_TIMER_CTRL),
	regmap_reg_range(AXP22X_GPIO_STATE, AXP22X_GPIO_STATE),
	regmap_reg_range(AXP288_RT_BATT_V_H, AXP288_RT_BATT_V_L),
	regmap_reg_range(AXP20X_FG_RES, AXP288_FG_CC_CAP_REG),
};

static const struct regmap_access_table axp288_writeable_table = {
	.yes_ranges	= axp288_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_writeable_ranges),
};

static const struct regmap_access_table axp288_volatile_table = {
	.yes_ranges	= axp288_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp288_volatile_ranges),
};

static const struct regmap_range axp806_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_DATACACHE(3)),
	regmap_reg_range(AXP806_PWR_OUT_CTRL1, AXP806_CLDO3_V_CTRL),
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ2_EN),
	regmap_reg_range(AXP20X_IRQ1_STATE, AXP20X_IRQ2_STATE),
	regmap_reg_range(AXP806_REG_ADDR_EXT, AXP806_REG_ADDR_EXT),
};

static const struct regmap_range axp806_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_STATE, AXP20X_IRQ2_STATE),
};

static const struct regmap_access_table axp806_writeable_table = {
	.yes_ranges	= axp806_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp806_writeable_ranges),
};

static const struct regmap_access_table axp806_volatile_table = {
	.yes_ranges	= axp806_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp806_volatile_ranges),
};

static struct resource axp152_pek_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP152_IRQ_PEK_RIS_EDGE, "PEK_DBR"),
	DEFINE_RES_IRQ_NAMED(AXP152_IRQ_PEK_FAL_EDGE, "PEK_DBF"),
};

static struct resource axp20x_ac_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_PLUGIN, "ACIN_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_REMOVAL, "ACIN_REMOVAL"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_ACIN_OVER_V, "ACIN_OVER_V"),
};

static struct resource axp20x_pek_resources[] = {
	{
		.name	= "PEK_DBR",
		.start	= AXP20X_IRQ_PEK_RIS_EDGE,
		.end	= AXP20X_IRQ_PEK_RIS_EDGE,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "PEK_DBF",
		.start	= AXP20X_IRQ_PEK_FAL_EDGE,
		.end	= AXP20X_IRQ_PEK_FAL_EDGE,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource axp20x_usb_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_PLUGIN, "VBUS_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_REMOVAL, "VBUS_REMOVAL"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_VALID, "VBUS_VALID"),
	DEFINE_RES_IRQ_NAMED(AXP20X_IRQ_VBUS_NOT_VALID, "VBUS_NOT_VALID"),
};

static struct resource axp22x_usb_power_supply_resources[] = {
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_VBUS_PLUGIN, "VBUS_PLUGIN"),
	DEFINE_RES_IRQ_NAMED(AXP22X_IRQ_VBUS_REMOVAL, "VBUS_REMOVAL"),
};

static struct resource axp22x_pek_resources[] = {
	{
		.name   = "PEK_DBR",
		.start  = AXP22X_IRQ_PEK_RIS_EDGE,
		.end    = AXP22X_IRQ_PEK_RIS_EDGE,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "PEK_DBF",
		.start  = AXP22X_IRQ_PEK_FAL_EDGE,
		.end    = AXP22X_IRQ_PEK_FAL_EDGE,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource axp288_power_button_resources[] = {
	{
		.name	= "PEK_DBR",
		.start	= AXP288_IRQ_POKP,
		.end	= AXP288_IRQ_POKP,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "PEK_DBF",
		.start	= AXP288_IRQ_POKN,
		.end	= AXP288_IRQ_POKN,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource axp288_fuel_gauge_resources[] = {
	{
		.start = AXP288_IRQ_QWBTU,
		.end   = AXP288_IRQ_QWBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WBTU,
		.end   = AXP288_IRQ_WBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QWBTO,
		.end   = AXP288_IRQ_QWBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WBTO,
		.end   = AXP288_IRQ_WBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WL2,
		.end   = AXP288_IRQ_WL2,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_WL1,
		.end   = AXP288_IRQ_WL1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource axp803_pek_resources[] = {
	{
		.name   = "PEK_DBR",
		.start  = AXP803_IRQ_PEK_RIS_EDGE,
		.end    = AXP803_IRQ_PEK_RIS_EDGE,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "PEK_DBF",
		.start  = AXP803_IRQ_PEK_FAL_EDGE,
		.end    = AXP803_IRQ_PEK_FAL_EDGE,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource axp809_pek_resources[] = {
	{
		.name   = "PEK_DBR",
		.start  = AXP809_IRQ_PEK_RIS_EDGE,
		.end    = AXP809_IRQ_PEK_RIS_EDGE,
		.flags  = IORESOURCE_IRQ,
	}, {
		.name   = "PEK_DBF",
		.start  = AXP809_IRQ_PEK_FAL_EDGE,
		.end    = AXP809_IRQ_PEK_FAL_EDGE,
		.flags  = IORESOURCE_IRQ,
	},
};

static const struct regmap_config axp152_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp152_writeable_table,
	.volatile_table	= &axp152_volatile_table,
	.max_register	= AXP152_PWM1_DUTY_CYCLE,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp20x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp20x_writeable_table,
	.volatile_table	= &axp20x_volatile_table,
	.max_register	= AXP20X_OCV(AXP20X_OCV_MAX),
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp22x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp22x_writeable_table,
	.volatile_table	= &axp22x_volatile_table,
	.max_register	= AXP22X_BATLOW_THRES1,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp288_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp288_writeable_table,
	.volatile_table	= &axp288_volatile_table,
	.max_register	= AXP288_FG_TUNE5,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct regmap_config axp806_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp806_writeable_table,
	.volatile_table	= &axp806_volatile_table,
	.max_register	= AXP806_REG_ADDR_EXT,
	.cache_type	= REGCACHE_RBTREE,
};

#define INIT_REGMAP_IRQ(_variant, _irq, _off, _mask)			\
	[_variant##_IRQ_##_irq] = { .reg_offset = (_off), .mask = BIT(_mask) }

static const struct regmap_irq axp152_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP152, LDO0IN_CONNECT,		0, 6),
	INIT_REGMAP_IRQ(AXP152, LDO0IN_REMOVAL,		0, 5),
	INIT_REGMAP_IRQ(AXP152, ALDO0IN_CONNECT,	0, 3),
	INIT_REGMAP_IRQ(AXP152, ALDO0IN_REMOVAL,	0, 2),
	INIT_REGMAP_IRQ(AXP152, DCDC1_V_LOW,		1, 5),
	INIT_REGMAP_IRQ(AXP152, DCDC2_V_LOW,		1, 4),
	INIT_REGMAP_IRQ(AXP152, DCDC3_V_LOW,		1, 3),
	INIT_REGMAP_IRQ(AXP152, DCDC4_V_LOW,		1, 2),
	INIT_REGMAP_IRQ(AXP152, PEK_SHORT,		1, 1),
	INIT_REGMAP_IRQ(AXP152, PEK_LONG,		1, 0),
	INIT_REGMAP_IRQ(AXP152, TIMER,			2, 7),
	INIT_REGMAP_IRQ(AXP152, PEK_RIS_EDGE,		2, 6),
	INIT_REGMAP_IRQ(AXP152, PEK_FAL_EDGE,		2, 5),
	INIT_REGMAP_IRQ(AXP152, GPIO3_INPUT,		2, 3),
	INIT_REGMAP_IRQ(AXP152, GPIO2_INPUT,		2, 2),
	INIT_REGMAP_IRQ(AXP152, GPIO1_INPUT,		2, 1),
	INIT_REGMAP_IRQ(AXP152, GPIO0_INPUT,		2, 0),
};

static const struct regmap_irq axp20x_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP20X, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP20X, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP20X, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP20X, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP20X, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP20X, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP20X, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP20X, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP20X, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP20X, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP20X, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP20X, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP20X, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP20X, BATT_TEMP_HIGH,	        1, 1),
	INIT_REGMAP_IRQ(AXP20X, BATT_TEMP_LOW,	        1, 0),
	INIT_REGMAP_IRQ(AXP20X, DIE_TEMP_HIGH,	        2, 7),
	INIT_REGMAP_IRQ(AXP20X, CHARG_I_LOW,		2, 6),
	INIT_REGMAP_IRQ(AXP20X, DCDC1_V_LONG,	        2, 5),
	INIT_REGMAP_IRQ(AXP20X, DCDC2_V_LONG,	        2, 4),
	INIT_REGMAP_IRQ(AXP20X, DCDC3_V_LONG,	        2, 3),
	INIT_REGMAP_IRQ(AXP20X, PEK_SHORT,		2, 1),
	INIT_REGMAP_IRQ(AXP20X, PEK_LONG,		2, 0),
	INIT_REGMAP_IRQ(AXP20X, N_OE_PWR_ON,		3, 7),
	INIT_REGMAP_IRQ(AXP20X, N_OE_PWR_OFF,	        3, 6),
	INIT_REGMAP_IRQ(AXP20X, VBUS_VALID,		3, 5),
	INIT_REGMAP_IRQ(AXP20X, VBUS_NOT_VALID,	        3, 4),
	INIT_REGMAP_IRQ(AXP20X, VBUS_SESS_VALID,	3, 3),
	INIT_REGMAP_IRQ(AXP20X, VBUS_SESS_END,	        3, 2),
	INIT_REGMAP_IRQ(AXP20X, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP20X, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP20X, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP20X, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP20X, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP20X, GPIO3_INPUT,		4, 3),
	INIT_REGMAP_IRQ(AXP20X, GPIO2_INPUT,		4, 2),
	INIT_REGMAP_IRQ(AXP20X, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP20X, GPIO0_INPUT,		4, 0),
};

static const struct regmap_irq axp22x_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP22X, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP22X, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP22X, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP22X, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP22X, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP22X, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP22X, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP22X, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP22X, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP22X, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP22X, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP22X, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP22X, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP22X, BATT_TEMP_HIGH,	        1, 1),
	INIT_REGMAP_IRQ(AXP22X, BATT_TEMP_LOW,	        1, 0),
	INIT_REGMAP_IRQ(AXP22X, DIE_TEMP_HIGH,	        2, 7),
	INIT_REGMAP_IRQ(AXP22X, PEK_SHORT,		2, 1),
	INIT_REGMAP_IRQ(AXP22X, PEK_LONG,		2, 0),
	INIT_REGMAP_IRQ(AXP22X, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP22X, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP22X, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP22X, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP22X, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP22X, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP22X, GPIO0_INPUT,		4, 0),
};

/* some IRQs are compatible with axp20x models */
static const struct regmap_irq axp288_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP288, VBUS_FALL,              0, 2),
	INIT_REGMAP_IRQ(AXP288, VBUS_RISE,              0, 3),
	INIT_REGMAP_IRQ(AXP288, OV,                     0, 4),
	INIT_REGMAP_IRQ(AXP288, FALLING_ALT,            0, 5),
	INIT_REGMAP_IRQ(AXP288, RISING_ALT,             0, 6),
	INIT_REGMAP_IRQ(AXP288, OV_ALT,                 0, 7),

	INIT_REGMAP_IRQ(AXP288, DONE,                   1, 2),
	INIT_REGMAP_IRQ(AXP288, CHARGING,               1, 3),
	INIT_REGMAP_IRQ(AXP288, SAFE_QUIT,              1, 4),
	INIT_REGMAP_IRQ(AXP288, SAFE_ENTER,             1, 5),
	INIT_REGMAP_IRQ(AXP288, ABSENT,                 1, 6),
	INIT_REGMAP_IRQ(AXP288, APPEND,                 1, 7),

	INIT_REGMAP_IRQ(AXP288, QWBTU,                  2, 0),
	INIT_REGMAP_IRQ(AXP288, WBTU,                   2, 1),
	INIT_REGMAP_IRQ(AXP288, QWBTO,                  2, 2),
	INIT_REGMAP_IRQ(AXP288, WBTO,                   2, 3),
	INIT_REGMAP_IRQ(AXP288, QCBTU,                  2, 4),
	INIT_REGMAP_IRQ(AXP288, CBTU,                   2, 5),
	INIT_REGMAP_IRQ(AXP288, QCBTO,                  2, 6),
	INIT_REGMAP_IRQ(AXP288, CBTO,                   2, 7),

	INIT_REGMAP_IRQ(AXP288, WL2,                    3, 0),
	INIT_REGMAP_IRQ(AXP288, WL1,                    3, 1),
	INIT_REGMAP_IRQ(AXP288, GPADC,                  3, 2),
	INIT_REGMAP_IRQ(AXP288, OT,                     3, 7),

	INIT_REGMAP_IRQ(AXP288, GPIO0,                  4, 0),
	INIT_REGMAP_IRQ(AXP288, GPIO1,                  4, 1),
	INIT_REGMAP_IRQ(AXP288, POKO,                   4, 2),
	INIT_REGMAP_IRQ(AXP288, POKL,                   4, 3),
	INIT_REGMAP_IRQ(AXP288, POKS,                   4, 4),
	INIT_REGMAP_IRQ(AXP288, POKN,                   4, 5),
	INIT_REGMAP_IRQ(AXP288, POKP,                   4, 6),
	INIT_REGMAP_IRQ(AXP288, TIMER,                  4, 7),

	INIT_REGMAP_IRQ(AXP288, MV_CHNG,                5, 0),
	INIT_REGMAP_IRQ(AXP288, BC_USB_CHNG,            5, 1),
};

static const struct regmap_irq axp803_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP803, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP803, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP803, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP803, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP803, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP803, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP803, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP803, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP803, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP803, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP803, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_HIGH,	2, 7),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_HIGH_END,	2, 6),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_LOW,	2, 5),
	INIT_REGMAP_IRQ(AXP803, BATT_CHG_TEMP_LOW_END,	2, 4),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_HIGH,	2, 3),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_HIGH_END,	2, 2),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_LOW,	2, 1),
	INIT_REGMAP_IRQ(AXP803, BATT_ACT_TEMP_LOW_END,	2, 0),
	INIT_REGMAP_IRQ(AXP803, DIE_TEMP_HIGH,	        3, 7),
	INIT_REGMAP_IRQ(AXP803, GPADC,		        3, 2),
	INIT_REGMAP_IRQ(AXP803, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP803, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP803, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP803, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP803, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP803, PEK_SHORT,		4, 4),
	INIT_REGMAP_IRQ(AXP803, PEK_LONG,		4, 3),
	INIT_REGMAP_IRQ(AXP803, PEK_OVER_OFF,		4, 2),
	INIT_REGMAP_IRQ(AXP803, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP803, GPIO0_INPUT,		4, 0),
	INIT_REGMAP_IRQ(AXP803, BC_USB_CHNG,            5, 1),
	INIT_REGMAP_IRQ(AXP803, MV_CHNG,                5, 0),
};

static const struct regmap_irq axp806_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP806, DIE_TEMP_HIGH_LV1,	0, 0),
	INIT_REGMAP_IRQ(AXP806, DIE_TEMP_HIGH_LV2,	0, 1),
	INIT_REGMAP_IRQ(AXP806, DCDCA_V_LOW,		0, 3),
	INIT_REGMAP_IRQ(AXP806, DCDCB_V_LOW,		0, 4),
	INIT_REGMAP_IRQ(AXP806, DCDCC_V_LOW,		0, 5),
	INIT_REGMAP_IRQ(AXP806, DCDCD_V_LOW,		0, 6),
	INIT_REGMAP_IRQ(AXP806, DCDCE_V_LOW,		0, 7),
	INIT_REGMAP_IRQ(AXP806, PWROK_LONG,		1, 0),
	INIT_REGMAP_IRQ(AXP806, PWROK_SHORT,		1, 1),
	INIT_REGMAP_IRQ(AXP806, WAKEUP,			1, 4),
	INIT_REGMAP_IRQ(AXP806, PWROK_FALL,		1, 5),
	INIT_REGMAP_IRQ(AXP806, PWROK_RISE,		1, 6),
};

static const struct regmap_irq axp809_regmap_irqs[] = {
	INIT_REGMAP_IRQ(AXP809, ACIN_OVER_V,		0, 7),
	INIT_REGMAP_IRQ(AXP809, ACIN_PLUGIN,		0, 6),
	INIT_REGMAP_IRQ(AXP809, ACIN_REMOVAL,	        0, 5),
	INIT_REGMAP_IRQ(AXP809, VBUS_OVER_V,		0, 4),
	INIT_REGMAP_IRQ(AXP809, VBUS_PLUGIN,		0, 3),
	INIT_REGMAP_IRQ(AXP809, VBUS_REMOVAL,	        0, 2),
	INIT_REGMAP_IRQ(AXP809, VBUS_V_LOW,		0, 1),
	INIT_REGMAP_IRQ(AXP809, BATT_PLUGIN,		1, 7),
	INIT_REGMAP_IRQ(AXP809, BATT_REMOVAL,	        1, 6),
	INIT_REGMAP_IRQ(AXP809, BATT_ENT_ACT_MODE,	1, 5),
	INIT_REGMAP_IRQ(AXP809, BATT_EXIT_ACT_MODE,	1, 4),
	INIT_REGMAP_IRQ(AXP809, CHARG,		        1, 3),
	INIT_REGMAP_IRQ(AXP809, CHARG_DONE,		1, 2),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_HIGH,	2, 7),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_HIGH_END,	2, 6),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_LOW,	2, 5),
	INIT_REGMAP_IRQ(AXP809, BATT_CHG_TEMP_LOW_END,	2, 4),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_HIGH,	2, 3),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_HIGH_END,	2, 2),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_LOW,	2, 1),
	INIT_REGMAP_IRQ(AXP809, BATT_ACT_TEMP_LOW_END,	2, 0),
	INIT_REGMAP_IRQ(AXP809, DIE_TEMP_HIGH,	        3, 7),
	INIT_REGMAP_IRQ(AXP809, LOW_PWR_LVL1,	        3, 1),
	INIT_REGMAP_IRQ(AXP809, LOW_PWR_LVL2,	        3, 0),
	INIT_REGMAP_IRQ(AXP809, TIMER,		        4, 7),
	INIT_REGMAP_IRQ(AXP809, PEK_RIS_EDGE,	        4, 6),
	INIT_REGMAP_IRQ(AXP809, PEK_FAL_EDGE,	        4, 5),
	INIT_REGMAP_IRQ(AXP809, PEK_SHORT,		4, 4),
	INIT_REGMAP_IRQ(AXP809, PEK_LONG,		4, 3),
	INIT_REGMAP_IRQ(AXP809, PEK_OVER_OFF,		4, 2),
	INIT_REGMAP_IRQ(AXP809, GPIO1_INPUT,		4, 1),
	INIT_REGMAP_IRQ(AXP809, GPIO0_INPUT,		4, 0),
};

static const struct regmap_irq_chip axp152_regmap_irq_chip = {
	.name			= "axp152_irq_chip",
	.status_base		= AXP152_IRQ1_STATE,
	.ack_base		= AXP152_IRQ1_STATE,
	.mask_base		= AXP152_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp152_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp152_regmap_irqs),
	.num_regs		= 3,
};

static const struct regmap_irq_chip axp20x_regmap_irq_chip = {
	.name			= "axp20x_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp20x_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp20x_regmap_irqs),
	.num_regs		= 5,

};

static const struct regmap_irq_chip axp22x_regmap_irq_chip = {
	.name			= "axp22x_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp22x_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp22x_regmap_irqs),
	.num_regs		= 5,
};

static const struct regmap_irq_chip axp288_regmap_irq_chip = {
	.name			= "axp288_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp288_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp288_regmap_irqs),
	.num_regs		= 6,

};

static const struct regmap_irq_chip axp803_regmap_irq_chip = {
	.name			= "axp803",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp803_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp803_regmap_irqs),
	.num_regs		= 6,
};

static const struct regmap_irq_chip axp806_regmap_irq_chip = {
	.name			= "axp806",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp806_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp806_regmap_irqs),
	.num_regs		= 2,
};

static const struct regmap_irq_chip axp809_regmap_irq_chip = {
	.name			= "axp809",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.mask_invert		= true,
	.init_ack_masked	= true,
	.irqs			= axp809_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp809_regmap_irqs),
	.num_regs		= 5,
};

static struct mfd_cell axp20x_cells[] = {
	{
		.name		= "axp20x-gpio",
		.of_compatible	= "x-powers,axp209-gpio",
	}, {
		.name		= "axp20x-pek",
		.num_resources	= ARRAY_SIZE(axp20x_pek_resources),
		.resources	= axp20x_pek_resources,
	}, {
		.name		= "axp20x-regulator",
	}, {
		.name		= "axp20x-adc",
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp209-battery-power-supply",
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp202-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp202-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_usb_power_supply_resources),
		.resources	= axp20x_usb_power_supply_resources,
	},
};

static struct mfd_cell axp221_cells[] = {
	{
		.name		= "axp221-pek",
		.num_resources	= ARRAY_SIZE(axp22x_pek_resources),
		.resources	= axp22x_pek_resources,
	}, {
		.name		= "axp20x-regulator",
	}, {
		.name		= "axp22x-adc"
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp221-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp221-battery-power-supply",
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp221-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp22x_usb_power_supply_resources),
		.resources	= axp22x_usb_power_supply_resources,
	},
};

static struct mfd_cell axp223_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp22x_pek_resources),
		.resources		= axp22x_pek_resources,
	}, {
		.name		= "axp22x-adc",
	}, {
		.name		= "axp20x-battery-power-supply",
		.of_compatible	= "x-powers,axp221-battery-power-supply",
	}, {
		.name			= "axp20x-regulator",
	}, {
		.name		= "axp20x-ac-power-supply",
		.of_compatible	= "x-powers,axp221-ac-power-supply",
		.num_resources	= ARRAY_SIZE(axp20x_ac_power_supply_resources),
		.resources	= axp20x_ac_power_supply_resources,
	}, {
		.name		= "axp20x-usb-power-supply",
		.of_compatible	= "x-powers,axp223-usb-power-supply",
		.num_resources	= ARRAY_SIZE(axp22x_usb_power_supply_resources),
		.resources	= axp22x_usb_power_supply_resources,
	},
};

static struct mfd_cell axp152_cells[] = {
	{
		.name			= "axp20x-pek",
		.num_resources		= ARRAY_SIZE(axp152_pek_resources),
		.resources		= axp152_pek_resources,
	},
};

static struct resource axp288_adc_resources[] = {
	{
		.name  = "GPADC",
		.start = AXP288_IRQ_GPADC,
		.end   = AXP288_IRQ_GPADC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource axp288_extcon_resources[] = {
	{
		.start = AXP288_IRQ_VBUS_FALL,
		.end   = AXP288_IRQ_VBUS_FALL,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_VBUS_RISE,
		.end   = AXP288_IRQ_VBUS_RISE,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_MV_CHNG,
		.end   = AXP288_IRQ_MV_CHNG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_BC_USB_CHNG,
		.end   = AXP288_IRQ_BC_USB_CHNG,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource axp288_charger_resources[] = {
	{
		.start = AXP288_IRQ_OV,
		.end   = AXP288_IRQ_OV,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_DONE,
		.end   = AXP288_IRQ_DONE,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CHARGING,
		.end   = AXP288_IRQ_CHARGING,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_SAFE_QUIT,
		.end   = AXP288_IRQ_SAFE_QUIT,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_SAFE_ENTER,
		.end   = AXP288_IRQ_SAFE_ENTER,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QCBTU,
		.end   = AXP288_IRQ_QCBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CBTU,
		.end   = AXP288_IRQ_CBTU,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_QCBTO,
		.end   = AXP288_IRQ_QCBTO,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = AXP288_IRQ_CBTO,
		.end   = AXP288_IRQ_CBTO,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell axp288_cells[] = {
	{
		.name = "axp288_adc",
		.num_resources = ARRAY_SIZE(axp288_adc_resources),
		.resources = axp288_adc_resources,
	},
	{
		.name = "axp288_extcon",
		.num_resources = ARRAY_SIZE(axp288_extcon_resources),
		.resources = axp288_extcon_resources,
	},
	{
		.name = "axp288_charger",
		.num_resources = ARRAY_SIZE(axp288_charger_resources),
		.resources = axp288_charger_resources,
	},
	{
		.name = "axp288_fuel_gauge",
		.num_resources = ARRAY_SIZE(axp288_fuel_gauge_resources),
		.resources = axp288_fuel_gauge_resources,
	},
	{
		.name = "axp221-pek",
		.num_resources = ARRAY_SIZE(axp288_power_button_resources),
		.resources = axp288_power_button_resources,
	},
	{
		.name = "axp288_pmic_acpi",
	},
};

static struct mfd_cell axp803_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp803_pek_resources),
		.resources		= axp803_pek_resources,
	},
	{	.name			= "axp20x-regulator" },
};

static struct mfd_cell axp806_cells[] = {
	{
		.id			= 2,
		.name			= "axp20x-regulator",
	},
};

static struct mfd_cell axp809_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp809_pek_resources),
		.resources		= axp809_pek_resources,
	}, {
		.id			= 1,
		.name			= "axp20x-regulator",
	},
};

static struct mfd_cell axp813_cells[] = {
	{
		.name			= "axp221-pek",
		.num_resources		= ARRAY_SIZE(axp803_pek_resources),
		.resources		= axp803_pek_resources,
	}
};

static struct axp20x_dev *axp20x_pm_power_off;
static void axp20x_power_off(void)
{
	if (axp20x_pm_power_off->variant == AXP288_ID)
		return;

	regmap_write(axp20x_pm_power_off->regmap, AXP20X_OFF_CTRL,
		     AXP20X_OFF);

	/* Give capacitors etc. time to drain to avoid kernel panic msg. */
	msleep(500);
}

#define kobj_to_device(x) container_of(x, struct device, kobj)

int axp20x_get_adc_freq(struct axp20x_dev *axp)
{
	unsigned int res;
	int ret, freq = 25;

	ret = regmap_read(axp->regmap, AXP20X_ADC_RATE, &res);
	if (ret < 0) {
		dev_warn(axp->dev, "Unable to read ADC sampling frequency: %d\n", ret);
		return freq;
	}
	switch ((res & 0xC0) >> 6) {
	case 0:
		freq = 25;
		break;
	case 1:
		freq = 50;
		break;
	case 2:
		freq = 100;
		break;
	case 3:
		freq = 200;
		break;
	}
	return freq;
}

static ssize_t axp20x_sysfs_read_bin_file(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	int ret;

	struct device *dev = kobj_to_device(kobj);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	ret = regmap_raw_read(axp->regmap, AXP20X_OCV(off), buf, count);
	if (ret < 0)
	{
		dev_warn(axp->dev, "read_bin_file: error reading: %d\n", ret);
		return ret;
	}
	return count;
}

static ssize_t axp20x_sysfs_write_bin_file(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	int ret;

	struct device *dev = kobj_to_device(kobj);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	ret = regmap_raw_write(axp->regmap, AXP20X_OCV(off), buf, count);
	if (ret < 0)
	{
		dev_warn(axp->dev, "write_bin_file: error writing: %d\n", ret);
		return ret;
	}
	return count;
}

static ssize_t axp20x_read_special(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i, freq, ret = 0;
	unsigned int res;
	u32 lval1, lval2;
	s64 llval;
	u64 ullval;

	const char *subsystem = kobject_name(kobj);
	struct device *dev = kobj_to_device(kobj->parent);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	dev_dbg(axp->dev, "read_special: reading attribute %s of object %s\n", attr->attr.name, subsystem);

	if (strcmp(subsystem, "battery") == 0) {
		if (strcmp(attr->attr.name, "power") == 0) {
			lval1 = 0;
			for (i = 0; i < 3; i++) {
				ret |= regmap_read(axp->regmap, AXP20X_PWR_BATT_H + i, &res);
				lval1 |= res << ((2 - i) * 8);
			}
			llval = lval1 * 1100 / 1000;
		} else if (strcmp(attr->attr.name, "charge") == 0) {
			ret = regmap_raw_read(axp->regmap, AXP20X_CHRG_CC_31_24, &lval1, sizeof(lval1));
			ret |= regmap_raw_read(axp->regmap, AXP20X_DISCHRG_CC_31_24, &lval2, sizeof(lval2));
			be32_to_cpus(&lval1);
			be32_to_cpus(&lval2);
			ullval = abs((s64)lval1 - (s64)lval2) * 65536 * 500;
			freq = axp20x_get_adc_freq(axp);
			do_div(ullval, 3600 * freq);
			llval = (lval1 < lval2) ? -ullval : ullval;
		} else if (strcmp(attr->attr.name, "capacity") == 0) {
			ret = regmap_read(axp->regmap, AXP20X_FG_RES, &res);
			llval = res & 0x7f;
		} else
			return -EINVAL;
	} else
		return -EINVAL;

	if (ret < 0) {
		dev_warn(axp->dev, "Unable to read parameter: %d\n", ret);
		return ret;
	}
	return sprintf(buf, "%lld\n", llval);
}

static ssize_t axp20x_write_int(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int reg, var, ret = 0, scale, width = 12, offset = 0;
	unsigned int res;

	const char *subsystem = kobject_name(kobj);
	struct device *dev = kobj_to_device(kobj->parent);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	dev_dbg(axp->dev, "write_int: writing attribute %s of object %s\n", attr->attr.name, subsystem);

	ret = kstrtoint(buf, 10, &var);
	if (ret < 0)
		return ret;

	if (strcmp(subsystem, "control") == 0) {
		if (strcmp(attr->attr.name, "battery_rdc") == 0) {
			reg = AXP20X_RDC_H;
			scale = 1074;
			width = 13;
			offset = 537;
			/* TODO: Disable & enable fuel gauge */
		} else
			return -EINVAL;
	} else
		return -EINVAL;

	res = (var + offset) / scale;

	ret = regmap_write_bits(axp->regmap, reg, (1U << (width - 8)) - 1, (res >> 8) & 0xFF);
	ret |= regmap_write_bits(axp->regmap, reg + 1, 0xFF, res & 0xFF);

	if (ret < 0) {
		dev_warn(axp->dev, "Unable to write parameter: %d\n", ret);
		return ret;
	}
	return count;
}

static ssize_t axp20x_read_bool(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int val, ret, reg, bit;
	unsigned int res;

	const char *subsystem = kobject_name(kobj);
	struct device *dev = kobj_to_device(kobj->parent);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	dev_dbg(axp->dev, "read_bool: reading attribute %s of object %s\n", attr->attr.name, subsystem);

	if (strcmp(subsystem, "ac") == 0) {
		if (strcmp(attr->attr.name, "connected") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 7;
		} else if (strcmp(attr->attr.name, "used") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 6;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "vbus") == 0) {
		if (strcmp(attr->attr.name, "connected") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 5;
		} else if (strcmp(attr->attr.name, "used") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 4;
		} else if (strcmp(attr->attr.name, "strong") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 3;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "battery") == 0) {
		if (strcmp(attr->attr.name, "connected") == 0) {
			reg = AXP20X_PWR_OP_MODE;
			bit = 5;
		} else if (strcmp(attr->attr.name, "charging") == 0) {
			reg = AXP20X_PWR_INPUT_STATUS;
			bit = 2;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "pmu") == 0) {
		if (strcmp(attr->attr.name, "overheat") == 0) {
			reg = AXP20X_PWR_OP_MODE;
			bit = 7;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "charger") == 0) {
		if (strcmp(attr->attr.name, "charging") == 0) {
			reg = AXP20X_PWR_OP_MODE;
			bit = 6;
		} else if (strcmp(attr->attr.name, "cell_activation") == 0) {
			reg = AXP20X_PWR_OP_MODE;
			bit = 3;
		} else if (strcmp(attr->attr.name, "low_power") == 0) {
			reg = AXP20X_PWR_OP_MODE;
			bit = 2;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "control") == 0) {
		if (strcmp(attr->attr.name, "set_vbus_direct_mode") == 0) {
			reg = AXP20X_VBUS_IPSOUT_MGMT;
			bit = 6;
		} else if (strcmp(attr->attr.name, "reset_charge_counter") == 0) {
			reg = AXP20X_CC_CTRL;
			bit = 5;
		} else if (strcmp(attr->attr.name, "charge_rtc_battery") == 0) {
			reg = AXP20X_CHRG_BAK_CTRL;
			bit = 7;
		} else if (strcmp(attr->attr.name, "disable_fuel_gauge") == 0) {
			reg = AXP20X_FG_RES;
			bit = 7;
		} else
			return -EINVAL;
	} else
		return -EINVAL;

	ret = regmap_read(axp->regmap, reg, &res);
	if (ret < 0) {
		dev_warn(axp->dev, "Unable to read parameter: %d\n", ret);
		return ret;
	}
	val = (res & BIT(bit)) == BIT(bit) ? 1 : 0;
	return sprintf(buf, "%d\n", val);
}

static ssize_t axp20x_write_bool(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int var, ret, reg, bit;

	const char *subsystem = kobject_name(kobj);
	struct device *dev = kobj_to_device(kobj->parent);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	dev_dbg(axp->dev, "write_bool: writing attribute %s of object %s", attr->attr.name, subsystem);

	ret = kstrtoint(buf, 10, &var);
	if (ret < 0)
		return ret;

	if (strcmp(subsystem, "control") == 0) {
		if (strcmp(attr->attr.name, "set_vbus_direct_mode") == 0) {
			reg = AXP20X_VBUS_IPSOUT_MGMT;
			bit = 6;
		} else if (strcmp(attr->attr.name, "reset_charge_counter") == 0) {
			reg = AXP20X_CC_CTRL;
			bit = 5;
		} else if (strcmp(attr->attr.name, "charge_rtc_battery") == 0) {
			reg = AXP20X_CHRG_BAK_CTRL;
			bit = 7;
		} else if (strcmp(attr->attr.name, "disable_fuel_gauge") == 0) {
			reg = AXP20X_FG_RES;
			bit = 7;
		} else
			return -EINVAL;
	} else
		return -EINVAL;

	ret = regmap_update_bits(axp->regmap, reg, BIT(bit), var ? BIT(bit) : 0);
	if (ret)
		dev_warn(axp->dev, "Unable to write value: %d", ret);
	return count;
}

static int axp20x_averaging_helper(struct regmap *reg_map, int reg_h,
	int width)
{
	long acc = 0;
	int ret, i;

	for (i = 0; i < 3; i++) {
		ret = axp20x_read_variable_width(reg_map, reg_h, width);
		if (ret < 0)
			return ret;
		acc += ret;
		msleep(20); /* For 100Hz sampling frequency */
	}
	return (int)(acc / 3);
}

static ssize_t axp20x_read_int(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int val, ret, scale, reg, width = 12, offset = 0;

	const char *subsystem = kobject_name(kobj);
	struct device *dev = kobj_to_device(kobj->parent);
	struct axp20x_dev *axp = dev_get_drvdata(dev);

	dev_dbg(axp->dev, "read_int: reading attribute %s of object %s\n", attr->attr.name, subsystem);

	if (strcmp(subsystem, "ac") == 0) {
		if (strcmp(attr->attr.name, "voltage") == 0) {
			reg = AXP20X_ACIN_V_ADC_H;
			scale = 1700;
		} else if (strcmp(attr->attr.name, "amperage") == 0) {
			reg = AXP20X_ACIN_I_ADC_H;
			scale = 625;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "vbus") == 0) {
		if (strcmp(attr->attr.name, "voltage") == 0) {
			reg = AXP20X_VBUS_V_ADC_H;
			scale = 1700;
		} else if (strcmp(attr->attr.name, "amperage") == 0) {
			reg = AXP20X_VBUS_I_ADC_H;
			scale = 375;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "battery") == 0) {
		if (strcmp(attr->attr.name, "voltage") == 0) {
			reg = AXP20X_BATT_V_H;
			scale = 1100;
		} else if (strcmp(attr->attr.name, "amperage") == 0) {
			reg = AXP20X_BATT_DISCHRG_I_H;
			scale = 500;
			width = 13;
		} else if (strcmp(attr->attr.name, "ts_voltage") == 0) {
			reg = AXP20X_TS_IN_H;
			scale = 800;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "pmu") == 0) {
		if (strcmp(attr->attr.name, "temp") == 0) {
			reg = AXP20X_TEMP_ADC_H;
			scale = 100;
			offset = 144700;
		} else if (strcmp(attr->attr.name, "voltage") == 0) {
			reg = AXP20X_IPSOUT_V_HIGH_H;
			scale = 1400;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "charger") == 0) {
		if (strcmp(attr->attr.name, "amperage") == 0) {
			reg = AXP20X_BATT_CHRG_I_H;
			scale = 500;
		} else
			return -EINVAL;
	} else if (strcmp(subsystem, "control") == 0) {
		if (strcmp(attr->attr.name, "battery_rdc") == 0) {
			reg = AXP20X_RDC_H;
			width = 13;
			scale = 1074;
			offset = 537;
		} else
			return -EINVAL;
	} else
		return -EINVAL;

	ret = axp20x_averaging_helper(axp->regmap, reg, width);

	if (ret < 0) {
		dev_warn(axp->dev, "Unable to read parameter: %d\n", ret);
		return ret;
	}
	val = ret * scale - offset;
	return sprintf(buf, "%d\n", val);
}

/* AC IN */
static struct kobj_attribute ac_in_voltage = __ATTR(voltage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute ac_in_amperage = __ATTR(amperage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute ac_in_connected = __ATTR(connected, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute ac_in_used = __ATTR(used, S_IRUGO, axp20x_read_bool, NULL);

static struct attribute *axp20x_attributes_ac[] = {
	&ac_in_voltage.attr,
	&ac_in_amperage.attr,
	&ac_in_connected.attr,
	&ac_in_used.attr,
	NULL,
};

static const struct attribute_group axp20x_group_ac = {
	.attrs = axp20x_attributes_ac,
};

/* Vbus */
static struct kobj_attribute vbus_voltage = __ATTR(voltage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute vbus_amperage = __ATTR(amperage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute vbus_connected = __ATTR(connected, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute vbus_used = __ATTR(used, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute vbus_strong = __ATTR(strong, S_IRUGO, axp20x_read_bool, NULL);

static struct attribute *axp20x_attributes_vbus[] = {
	&vbus_voltage.attr,
	&vbus_amperage.attr,
	&vbus_connected.attr,
	&vbus_used.attr,
	&vbus_strong.attr,
	NULL,
};

static const struct attribute_group axp20x_group_vbus = {
	.attrs = axp20x_attributes_vbus,
};

/* Battery */
static struct kobj_attribute batt_voltage = __ATTR(voltage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute batt_amperage = __ATTR(amperage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute batt_ts_voltage = __ATTR(ts_voltage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute batt_power = __ATTR(power, S_IRUGO, axp20x_read_special, NULL);
static struct kobj_attribute batt_charge = __ATTR(charge, S_IRUGO, axp20x_read_special, NULL);
static struct kobj_attribute batt_capacity = __ATTR(capacity, S_IRUGO, axp20x_read_special, NULL);
static struct kobj_attribute batt_connected = __ATTR(connected, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute batt_charging = __ATTR(charging, S_IRUGO, axp20x_read_bool, NULL);

static struct attribute *axp20x_attributes_battery[] = {
	&batt_voltage.attr,
	&batt_amperage.attr,
	&batt_ts_voltage.attr,
	&batt_power.attr,
	&batt_charge.attr,
	&batt_capacity.attr,
	&batt_connected.attr,
	&batt_charging.attr,
	NULL,
};

static const struct attribute_group axp20x_group_battery = {
	.attrs = axp20x_attributes_battery,
};

/* PMU */
static struct kobj_attribute pmu_temp = __ATTR(temp, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute pmu_voltage = __ATTR(voltage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute pmu_overheat = __ATTR(overheat, S_IRUGO, axp20x_read_bool, NULL);

static struct attribute *axp20x_attributes_pmu[] = {
	&pmu_temp.attr,
	&pmu_voltage.attr,
	&pmu_overheat.attr,
	NULL,
};

static const struct attribute_group axp20x_group_pmu = {
	.attrs = axp20x_attributes_pmu,
};

/* Charger */
static struct kobj_attribute charger_amperage = __ATTR(amperage, S_IRUGO, axp20x_read_int, NULL);
static struct kobj_attribute charger_charging = __ATTR(charging, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute charger_cell_activation = __ATTR(cell_activation, S_IRUGO, axp20x_read_bool, NULL);
static struct kobj_attribute charger_low_power = __ATTR(low_power, S_IRUGO, axp20x_read_bool, NULL);

static struct attribute *axp20x_attributes_charger[] = {
	&charger_amperage.attr,
	&charger_charging.attr,
	&charger_cell_activation.attr,
	&charger_low_power.attr,
	NULL,
};

static const struct attribute_group axp20x_group_charger = {
	.attrs = axp20x_attributes_charger,
};

/* Control (writeable) */
static struct kobj_attribute control_vbus_direct_mode = __ATTR(set_vbus_direct_mode, (S_IRUGO | S_IWUSR),
	axp20x_read_bool, axp20x_write_bool);
static struct kobj_attribute control_reset_charge_counter = __ATTR(reset_charge_counter, (S_IRUGO | S_IWUSR),
	axp20x_read_bool, axp20x_write_bool);
static struct kobj_attribute control_charge_rtc_battery = __ATTR(charge_rtc_battery, (S_IRUGO | S_IWUSR),
	axp20x_read_bool, axp20x_write_bool);
static struct kobj_attribute control_disable_fuel_gauge = __ATTR(disable_fuel_gauge, (S_IRUGO | S_IWUSR),
	axp20x_read_bool, axp20x_write_bool);
static struct kobj_attribute control_battery_rdc = __ATTR(battery_rdc, (S_IRUGO | S_IWUSR),
	axp20x_read_int, axp20x_write_int);

static struct attribute *axp20x_attributes_control[] = {
	&control_vbus_direct_mode.attr,
	&control_reset_charge_counter.attr,
	&control_charge_rtc_battery.attr,
	&control_disable_fuel_gauge.attr,
	&control_battery_rdc.attr,
	NULL,
};

static const struct attribute_group axp20x_group_control = {
	.attrs = axp20x_attributes_control,
};

static struct {
	struct kobject *ac;
	struct kobject *vbus;
	struct kobject *battery;
	struct kobject *pmu;
	struct kobject *charger;
	struct kobject *control;
} subsystems;

static struct bin_attribute axp20x_ocv_curve = __BIN_ATTR(ocv_curve, S_IRUGO | S_IWUSR,
	axp20x_sysfs_read_bin_file, axp20x_sysfs_write_bin_file, AXP20X_OCV_MAX + 1);

static void axp20x_sysfs_create_subgroup(const char name[], struct axp20x_dev *axp,
	struct kobject *subgroup, const struct attribute_group *attrs)
{
	int ret;
	struct kobject *parent = &axp->dev->kobj;
	subgroup = kobject_create_and_add(name, parent);
	if (subgroup != NULL) {
		ret = sysfs_create_group(subgroup, attrs);
		if (ret) {
			dev_warn(axp->dev, "Unable to register sysfs group: %s: %d", name, ret);
			kobject_put(subgroup);
		}
	}
}

static void axp20x_sysfs_remove_subgroup(struct kobject *subgroup,
	const struct attribute_group *attrs)
{
	sysfs_remove_group(subgroup, attrs);
	kobject_put(subgroup);
}

static int axp20x_sysfs_init(struct axp20x_dev *axp)
{
	int ret;
	unsigned int res;

	/* Enable all ADC channels in the first register */
	ret = regmap_write(axp->regmap, AXP20X_ADC_EN1, 0xFF);
	if (ret)
		dev_warn(axp->dev, "Unable to enable ADC: %d", ret);

	/*
	 * Set ADC sampling frequency to 100Hz (default is 25)
	 * Always measure battery temperature (default: only when charging)
	 */
	ret = regmap_update_bits(axp->regmap, AXP20X_ADC_RATE, 0xC3, 0x82);
	if (ret)
		dev_warn(axp->dev, "Unable to set ADC frequency and TS current output: %d", ret);

	/* Enable fuel gauge and charge counter */
	ret = regmap_update_bits(axp->regmap, AXP20X_FG_RES, 0x80, 0x00);
	if (ret)
		dev_warn(axp->dev, "Unable to enable battery fuel gauge: %d", ret);
	/* ret = regmap_update_bits(axp->regmap, AXP20X_CC_CTRL, 0xC0, 0x00); */
	ret |= regmap_update_bits(axp->regmap, AXP20X_CC_CTRL, 0xC0, 0x80);
	if (ret)
		dev_warn(axp->dev, "Unable to enable battery charge counter: %d", ret);

	/* Enable battery detection */
	ret = regmap_read(axp->regmap, AXP20X_OFF_CTRL, &res);
	if (ret == 0) {
		if ((res & 0x40) != 0x40) {
			dev_info(axp->dev, "Battery detection is disabled, enabling");
			ret = regmap_update_bits(axp->regmap, AXP20X_OFF_CTRL, 0x40, 0x40);
			if (ret)
				dev_warn(axp->dev, "Unable to enable battery detection: %d", ret);
		}
	} else
		dev_warn(axp->dev, "Unable to read register AXP20X_OFF_CTRL: %d", ret);

	/* Get info about backup (RTC) battery */
	ret = regmap_read(axp->regmap, AXP20X_CHRG_BAK_CTRL, &res);
	if (ret == 0) {
		dev_info(axp->dev, "Backup (RTC) battery charging is %s",
			(res & 0x80) == 0x80 ? "enabled" : "disabled");
		if ((res & 0x60) != 0x20)
			dev_warn(axp->dev, "Backup (RTC) battery target voltage is not 3.0V");
	} else
		dev_warn(axp->dev, "Unable to read register AXP20X_CHRG_BAK_CTRL: %d", ret);

	axp20x_sysfs_create_subgroup("ac", axp, subsystems.ac, &axp20x_group_ac);
	axp20x_sysfs_create_subgroup("vbus", axp, subsystems.vbus, &axp20x_group_vbus);
	axp20x_sysfs_create_subgroup("battery", axp, subsystems.battery, &axp20x_group_battery);
	axp20x_sysfs_create_subgroup("pmu", axp, subsystems.pmu, &axp20x_group_pmu);
	axp20x_sysfs_create_subgroup("charger", axp, subsystems.charger, &axp20x_group_charger);
	axp20x_sysfs_create_subgroup("control", axp, subsystems.control, &axp20x_group_control);

	ret = sysfs_create_bin_file(&axp->dev->kobj, &axp20x_ocv_curve);
	if (ret)
		dev_warn(axp->dev, "Unable to create sysfs ocv_curve file: %d", ret);

	ret = sysfs_create_link_nowarn(power_kobj, &axp->dev->kobj, "axp_pmu");
	if (ret)
		dev_warn(axp->dev, "Unable to create sysfs symlink: %d", ret);
	return ret;
}

static void axp20x_sysfs_exit(struct axp20x_dev *axp)
{
	sysfs_delete_link(power_kobj, &axp->dev->kobj, "axp_pmu");
	sysfs_remove_bin_file(&axp->dev->kobj, &axp20x_ocv_curve);
	axp20x_sysfs_remove_subgroup(subsystems.control, &axp20x_group_control);
	axp20x_sysfs_remove_subgroup(subsystems.charger, &axp20x_group_charger);
	axp20x_sysfs_remove_subgroup(subsystems.pmu, &axp20x_group_pmu);
	axp20x_sysfs_remove_subgroup(subsystems.battery, &axp20x_group_battery);
	axp20x_sysfs_remove_subgroup(subsystems.vbus, &axp20x_group_vbus);
	axp20x_sysfs_remove_subgroup(subsystems.ac, &axp20x_group_ac);
}

int axp20x_match_device(struct axp20x_dev *axp20x)
{
	struct device *dev = axp20x->dev;
	const struct acpi_device_id *acpi_id;
	const struct of_device_id *of_id;

	if (dev->of_node) {
		of_id = of_match_device(dev->driver->of_match_table, dev);
		if (!of_id) {
			dev_err(dev, "Unable to match OF ID\n");
			return -ENODEV;
		}
		axp20x->variant = (long)of_id->data;
	} else {
		acpi_id = acpi_match_device(dev->driver->acpi_match_table, dev);
		if (!acpi_id || !acpi_id->driver_data) {
			dev_err(dev, "Unable to match ACPI ID and data\n");
			return -ENODEV;
		}
		axp20x->variant = (long)acpi_id->driver_data;
	}

	switch (axp20x->variant) {
	case AXP152_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp152_cells);
		axp20x->cells = axp152_cells;
		axp20x->regmap_cfg = &axp152_regmap_config;
		axp20x->regmap_irq_chip = &axp152_regmap_irq_chip;
		break;
	case AXP202_ID:
	case AXP209_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp20x_cells);
		axp20x->cells = axp20x_cells;
		axp20x->regmap_cfg = &axp20x_regmap_config;
		axp20x->regmap_irq_chip = &axp20x_regmap_irq_chip;
		break;
	case AXP221_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp221_cells);
		axp20x->cells = axp221_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp22x_regmap_irq_chip;
		break;
	case AXP223_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp223_cells);
		axp20x->cells = axp223_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp22x_regmap_irq_chip;
		break;
	case AXP288_ID:
		axp20x->cells = axp288_cells;
		axp20x->nr_cells = ARRAY_SIZE(axp288_cells);
		axp20x->regmap_cfg = &axp288_regmap_config;
		axp20x->regmap_irq_chip = &axp288_regmap_irq_chip;
		axp20x->irq_flags = IRQF_TRIGGER_LOW;
		break;
	case AXP803_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp803_cells);
		axp20x->cells = axp803_cells;
		axp20x->regmap_cfg = &axp288_regmap_config;
		axp20x->regmap_irq_chip = &axp803_regmap_irq_chip;
		break;
	case AXP806_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp806_cells);
		axp20x->cells = axp806_cells;
		axp20x->regmap_cfg = &axp806_regmap_config;
		axp20x->regmap_irq_chip = &axp806_regmap_irq_chip;
		break;
	case AXP809_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp809_cells);
		axp20x->cells = axp809_cells;
		axp20x->regmap_cfg = &axp22x_regmap_config;
		axp20x->regmap_irq_chip = &axp809_regmap_irq_chip;
		break;
	case AXP813_ID:
		axp20x->nr_cells = ARRAY_SIZE(axp813_cells);
		axp20x->cells = axp813_cells;
		axp20x->regmap_cfg = &axp288_regmap_config;
		/*
		 * The IRQ table given in the datasheet is incorrect.
		 * In IRQ enable/status registers 1, there are separate
		 * IRQs for ACIN and VBUS, instead of bits [7:5] being
		 * the same as bits [4:2]. So it shares the same IRQs
		 * as the AXP803, rather than the AXP288.
		 */
		axp20x->regmap_irq_chip = &axp803_regmap_irq_chip;
		break;
	default:
		dev_err(dev, "unsupported AXP20X ID %lu\n", axp20x->variant);
		return -EINVAL;
	}
	dev_info(dev, "AXP20x variant %s found\n",
		 axp20x_model_names[axp20x->variant]);

	return 0;
}
EXPORT_SYMBOL(axp20x_match_device);

int axp20x_device_probe(struct axp20x_dev *axp20x)
{
	int ret;

	/*
	 * The AXP806 supports either master/standalone or slave mode.
	 * Slave mode allows sharing the serial bus, even with multiple
	 * AXP806 which all have the same hardware address.
	 *
	 * This is done with extra "serial interface address extension",
	 * or AXP806_BUS_ADDR_EXT, and "register address extension", or
	 * AXP806_REG_ADDR_EXT, registers. The former is read-only, with
	 * 1 bit customizable at the factory, and 1 bit depending on the
	 * state of an external pin. The latter is writable. The device
	 * will only respond to operations to its other registers when
	 * the these device addressing bits (in the upper 4 bits of the
	 * registers) match.
	 *
	 * By default we support an AXP806 chained to an AXP809 in slave
	 * mode. Boards which use an AXP806 in master mode can set the
	 * property "x-powers,master-mode" to override the default.
	 */
	if (axp20x->variant == AXP806_ID) {
		if (of_property_read_bool(axp20x->dev->of_node,
					  "x-powers,master-mode"))
			regmap_write(axp20x->regmap, AXP806_REG_ADDR_EXT,
				     AXP806_REG_ADDR_EXT_ADDR_MASTER_MODE);
		else
			regmap_write(axp20x->regmap, AXP806_REG_ADDR_EXT,
				     AXP806_REG_ADDR_EXT_ADDR_SLAVE_MODE);
	}

	ret = regmap_add_irq_chip(axp20x->regmap, axp20x->irq,
			  IRQF_ONESHOT | IRQF_SHARED | axp20x->irq_flags,
			   -1, axp20x->regmap_irq_chip, &axp20x->regmap_irqc);
	if (ret) {
		dev_err(axp20x->dev, "failed to add irq chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(axp20x->dev, -1, axp20x->cells,
			      axp20x->nr_cells, NULL, 0, NULL);

	if (ret) {
		dev_err(axp20x->dev, "failed to add MFD devices: %d\n", ret);
		regmap_del_irq_chip(axp20x->irq, axp20x->regmap_irqc);
		return ret;
	}

	if (!pm_power_off) {
		axp20x_pm_power_off = axp20x;
		pm_power_off = axp20x_power_off;
	}

	if (axp20x->variant == AXP209_ID || axp20x->variant == AXP202_ID) {
		axp20x_sysfs_init(axp20x);
	}

	dev_info(axp20x->dev, "AXP20X driver loaded\n");

	return 0;
}
EXPORT_SYMBOL(axp20x_device_probe);

int axp20x_device_remove(struct axp20x_dev *axp20x)
{
	if (axp20x == axp20x_pm_power_off) {
		axp20x_pm_power_off = NULL;
		pm_power_off = NULL;
	}

	if (axp20x->variant == AXP209_ID || axp20x->variant == AXP202_ID) {
		axp20x_sysfs_exit(axp20x);
	}

	mfd_remove_devices(axp20x->dev);
	regmap_del_irq_chip(axp20x->irq, axp20x->regmap_irqc);

	return 0;
}
EXPORT_SYMBOL(axp20x_device_remove);

MODULE_DESCRIPTION("PMIC MFD core driver for AXP20X");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
