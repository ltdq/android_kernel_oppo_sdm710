/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPLUS Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Charger IC management module for charger system framework.
*              Manage all charger IC and define abstarct function flow.
* Version    : 1.0
* Date       : 2015-06-22
*            : Fanhong.Kong@ProDrv.CHG
* ------------------------------ Revision History: --------------------------------
* <version>           <date>                <author>                       <desc>
* Revision 1.0       2015-06-22        Fanhong.Kong@ProDrv.CHG       Created for new architecture
* Revision 2.0       2018-04-14        Fanhong.Kong@ProDrv.CHG       Upgrade for SVOOC
***********************************************************************************/
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "oplus_charger.h"
#include "oplus_vooc.h"
#include "oplus_gauge.h"
#include "oplus_adapter.h"
#include "oplus_debug_info.h"

#define VOOC_NOTIFY_FAST_PRESENT			0x52
#define VOOC_NOTIFY_FAST_ABSENT				0x54
#define VOOC_NOTIFY_ALLOW_READING_IIC		0x58
#define VOOC_NOTIFY_NORMAL_TEMP_FULL		0x5a
#define VOOC_NOTIFY_LOW_TEMP_FULL			0x53
#define VOOC_NOTIFY_FIRMWARE_UPDATE			0x56
#define VOOC_NOTIFY_BAD_CONNECTED			0x59
#define VOOC_NOTIFY_TEMP_OVER				0x5c
#define VOOC_NOTIFY_ADAPTER_FW_UPDATE		0x5b
#define VOOC_NOTIFY_BTB_TEMP_OVER			0x5d
#define VOOC_NOTIFY_ADAPTER_MODEL_FACTORY	0x5e

extern int charger_abnormal_log;
extern int enable_charger_log;
#define vooc_xlog_printk(num, fmt, ...) \
	do { \
		if (enable_charger_log >= (int)num) { \
			printk(KERN_NOTICE pr_fmt("[OPLUS_CHG][%s]"fmt), __func__, ##__VA_ARGS__);\
	} \
} while (0)


static struct oplus_vooc_chip *g_vooc_chip = NULL;
bool __attribute__((weak)) oplus_get_fg_i2c_err_occured(void)
{
	return false;
}

void __attribute__((weak)) oplus_set_fg_i2c_err_occured(bool i2c_err)
{
	return;
}
int __attribute__((weak)) request_firmware_select(const struct firmware **firmware_p,
		const char *name, struct device *device)
{
	return 1;
}
int __attribute__((weak)) register_devinfo(char *name, struct manufacture_info *info)
{
	return 1;
}
static int oplus_vooc_convert_fast_chg_type(int fast_chg_type);

void oplus_vooc_battery_update()
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
/*
		if (!chip) {
			chg_err("  g_vooc_chip is NULL\n");
			return ;
		}
*/
	if (!chip->batt_psy) {
		chip->batt_psy = power_supply_get_by_name("battery");
	}
	if (chip->batt_psy) {
		power_supply_changed(chip->batt_psy);
	}
}

void oplus_vooc_switch_mode(int mode)
{
	if (!g_vooc_chip) {
		chg_err("  g_vooc_chip is NULL\n");
	} else {
		g_vooc_chip->vops->set_switch_mode(g_vooc_chip, mode);
	}
}

static void oplus_vooc_awake_init(struct oplus_vooc_chip *chip)
{
	if (!chip) {
		return;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->vooc_wake_lock, WAKE_LOCK_SUSPEND, "vooc_wake_lock");
#else
	chip->vooc_ws = wakeup_source_register("vooc_wake_lock");
#endif
}

static void oplus_vooc_set_awake(struct oplus_vooc_chip *chip, bool awake)
{
	static bool pm_flag = false;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if(!chip) {
		return;
	}
	if (awake && !pm_flag) {
		pm_flag = true;
		wake_lock(&chip->vooc_wake_lock);
	} else if (!awake && pm_flag)  {
		wake_unlock(&chip->vooc_wake_lock);
		pm_flag = false;
	}
#else
	if (!chip || !chip->vooc_ws) {
		return;
	}
	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->vooc_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->vooc_ws);
		pm_flag = false;
	}
#endif
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void oplus_vooc_watchdog(unsigned long data)
#else
static void oplus_vooc_watchdog(struct timer_list *unused)
#endif
{
        struct oplus_vooc_chip *chip = g_vooc_chip;

        if (!chip) {
                chg_err(" g_vooc_chip is NULL\n");
                return;
        }

	chg_err("watchdog bark: cannot receive mcu data\n");
	chip->allow_reading = true;
	chip->fastchg_started = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_low_temp_full = false;
	chip->btb_temp_over = false;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	charger_abnormal_log = CRITICAL_LOG_VOOC_WATCHDOG;
	schedule_work(&chip->vooc_watchdog_work);
}

static void oplus_vooc_init_watchdog_timer(struct oplus_vooc_chip *chip)
{
    if (!chip) {
        chg_err("oplus_vooc_chip is not ready\n");
        return;
    }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
    init_timer(&chip->watchdog);
    chip->watchdog.data = (unsigned long)chip;
    chip->watchdog.function = oplus_vooc_watchdog;
#else
    timer_setup(&chip->watchdog, oplus_vooc_watchdog, 0);
#endif
}

static void oplus_vooc_del_watchdog_timer(struct oplus_vooc_chip *chip)
{
    if (!chip) {
        chg_err("oplus_vooc_chip is not ready\n");
        return;
    }
    del_timer(&chip->watchdog);
}

static void oplus_vooc_setup_watchdog_timer(struct oplus_vooc_chip *chip, unsigned int ms)
{
    if (!chip) {
        chg_err("oplus_vooc_chip is not ready\n");
        return;
    }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
    mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
#else
    del_timer(&chip->watchdog);
    chip->watchdog.expires  = jiffies + msecs_to_jiffies(ms);
    add_timer(&chip->watchdog);
#endif
}

static void check_charger_out_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_vooc_chip *chip = container_of(dwork, struct oplus_vooc_chip, check_charger_out_work);
	int chg_vol = 0;

	chg_vol = oplus_chg_get_charger_voltage();
	if (chg_vol >= 0 && chg_vol < 2000) {
		chip->vops->reset_fastchg_after_usbout(chip);
		oplus_chg_clear_chargerid_info();
		oplus_vooc_battery_update();
		vooc_xlog_printk(CHG_LOG_CRTI, "charger out, chg_vol:%d\n", chg_vol);
	}
}

static void vooc_watchdog_work_func(struct work_struct *work)
{
	struct oplus_vooc_chip *chip = container_of(work,
		struct oplus_vooc_chip, vooc_watchdog_work);

	oplus_chg_set_chargerid_switch_val(0);
	oplus_chg_clear_chargerid_info();
	chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
	oplus_chg_set_charger_type_unknown();
	oplus_vooc_set_awake(chip, false);
}

static void oplus_vooc_check_charger_out(struct oplus_vooc_chip *chip)
{
	vooc_xlog_printk(CHG_LOG_CRTI, "  call\n");
	schedule_delayed_work(&chip->check_charger_out_work,
		round_jiffies_relative(msecs_to_jiffies(3000)));
}

int multistepCurrent[] = {3500, 2000, 3000, 4000, 5000, 6000, };

#define VOOC_TEMP_OVER_COUNTS	2

static int oplus_vooc_set_current_1_temp_normal_range(struct oplus_vooc_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;

	switch (chip->fastchg_batt_temp_status) {
		case BAT_TEMP_NATURAL:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp0) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
				ret = chip->vooc_strategy1_high_current0;
			} else if (vbat_temp_cur > chip->vooc_little_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
				ret = chip->vooc_strategy_normal_current;
			} else if (vbat_temp_cur > chip->vooc_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL_LOW;
				ret = chip->vooc_normal_to_little_cool_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			}
			break;
		case BAT_TEMP_HIGH0:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp1) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy1_high_current1;
			} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp0) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
				ret = chip->vooc_strategy1_low_current0;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
				ret = chip->vooc_strategy1_high_current0;
			}
			break;
		case BAT_TEMP_HIGH1:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp2) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy1_high_current2;
			} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp1) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
				ret = chip->vooc_strategy1_low_current1;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy1_high_current1;
			}
			break;
		case BAT_TEMP_HIGH2:
			if (chip->vooc_batt_over_high_temp != -EINVAL
					&& vbat_temp_cur > chip->vooc_batt_over_high_temp) {
				chip->vooc_strategy_change_count++;
				if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
					chip->vooc_strategy_change_count = 0;
					chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
					ret = chip->vooc_over_high_or_low_current;
				}
			}else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp2) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
				ret = chip->vooc_strategy1_low_current2;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy1_high_current2;;
			}
			break;
		case BAT_TEMP_LOW0:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp0) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
				ret = chip->vooc_strategy1_high_current0;
			} else if (vbat_temp_cur < chip->vooc_cool_temp) { //<12
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
				ret = chip->vooc_strategy1_low_current0;
			}
			break;
		case BAT_TEMP_LOW1:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp1) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy1_high_current1;
			} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp0) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
				ret = chip->vooc_strategy1_low_current0;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
				ret = chip->vooc_strategy1_low_current1;
			}
			break;
		case BAT_TEMP_LOW2:
			if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp2) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy1_high_current2;
			} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp1) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
				ret = chip->vooc_strategy1_low_current1;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
				ret = chip->vooc_strategy1_low_current2;
			}
			break;
		default:
			break;
	}
	vooc_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, status = %d\r\n", ret, vbat_temp_cur, chip->fastchg_batt_temp_status);
	return ret;
}

static int oplus_vooc_set_current_temp_little_cool_range(struct oplus_vooc_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	switch (chip->fastchg_batt_temp_status) {
		case BAT_TEMP_LITTLE_COOL:
			if (vbat_temp_cur < chip->vooc_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			} else if (vbat_temp_cur > chip->vooc_little_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			}
			break;
		case BAT_TEMP_LITTLE_COOL_LOW:
			if (vbat_temp_cur < chip->vooc_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			} else if(vbat_temp_cur > chip->vooc_little_cool_to_normal_temp){
				chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL_LOW;
				ret = chip->vooc_normal_to_little_cool_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			}
			break;
		default:
			break;
	}

	return ret;
}

static int oplus_vooc_set_current_temp_cool_range(struct oplus_vooc_chip *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->vooc_batt_over_low_temp != -EINVAL
			&& vbat_temp_cur < chip->vooc_batt_over_low_temp) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			ret = chip->vooc_over_high_or_low_current;
		}
	}
	if (vbat_temp_cur < chip->vooc_cool_temp) {
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		ret = chip->vooc_strategy_normal_current;
	} else if (vbat_temp_cur < chip->vooc_little_cool_temp) {
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		ret = chip->vooc_strategy_normal_current;
	} else {
		chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
		ret = chip->vooc_strategy_normal_current;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;
	}

	return ret;
}

static int oplus_vooc_set_current_when_bleow_setting_batt_temp
		(struct oplus_vooc_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_INIT) {
		if (vbat_temp_cur < chip->vooc_cool_temp) {
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		} else if(vbat_temp_cur < chip->vooc_little_cool_temp) {
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		} else {
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;
			chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
		}
	}

	switch (chip->vooc_temp_cur_range) {
		case FASTCHG_TEMP_RANGE_NORMAL:
			ret = oplus_vooc_set_current_1_temp_normal_range(chip, vbat_temp_cur);
			break;
		case FASTCHG_TEMP_RANGE_LITTLE_COOL:
			ret = oplus_vooc_set_current_temp_little_cool_range(chip, vbat_temp_cur);
			break;
		case FASTCHG_TEMP_RANGE_COOL:
			ret = oplus_vooc_set_current_temp_cool_range(chip, vbat_temp_cur);
			break;
		default:
			break;
	}

	vooc_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n", 
			ret, vbat_temp_cur, chip->fastchg_batt_temp_status, chip->vooc_temp_cur_range);
	return ret;
}

static int oplus_vooc_set_current_2_temp_normal_range(struct oplus_vooc_chip *chip, int vbat_temp_cur){
	int ret = 0;

	chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;

	switch (chip->fastchg_batt_temp_status) {
		case BAT_TEMP_NATURAL:
			if (vbat_temp_cur > chip->vooc_strategy2_batt_up_temp1) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
				ret = chip->vooc_strategy2_high0_current;
			} else if (vbat_temp_cur > chip->vooc_little_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
				ret = chip->vooc_strategy_normal_current;
			} else if (vbat_temp_cur > chip->vooc_cool_temp) {
				chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL_LOW;
				ret = chip->vooc_normal_to_little_cool_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			}
			break;
		case BAT_TEMP_HIGH0:
			if (vbat_temp_cur > chip->vooc_strategy2_batt_up_temp3) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy2_high1_current;
			} else if (vbat_temp_cur < chip->vooc_cool_temp) { //<12
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
				ret = chip->vooc_strategy2_high0_current;;
			}
			break;
		case BAT_TEMP_HIGH1:
			if (vbat_temp_cur > chip->vooc_strategy2_batt_up_temp5) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy2_high2_current;
			} else if (vbat_temp_cur < chip->vooc_cool_temp) { //<12
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				ret = chip->vooc_strategy_normal_current;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy2_high1_current;
			}
			break;
		case BAT_TEMP_HIGH2:
			if (vbat_temp_cur > chip->vooc_strategy2_batt_up_temp6) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH3;
				ret = chip->vooc_strategy2_high3_current;
			} else if (vbat_temp_cur < chip->vooc_strategy2_batt_up_down_temp2){
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
				ret = chip->vooc_strategy2_high1_current;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy2_high2_current;
			}
			break;
		case BAT_TEMP_HIGH3:
			if (chip->vooc_batt_over_high_temp != -EINVAL
					&& vbat_temp_cur > chip->vooc_batt_over_high_temp) {
				chip->vooc_strategy_change_count++;
				if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
					chip->vooc_strategy_change_count = 0;
					chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
					ret = chip->vooc_over_high_or_low_current;
				}
			}else if (vbat_temp_cur < chip->vooc_strategy2_batt_up_down_temp4) {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
				ret = chip->vooc_strategy2_high2_current;
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_HIGH3;
				ret = chip->vooc_strategy2_high3_current;
			}
			break;
		default:
			break;
	}
	vooc_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d\r\n", ret, vbat_temp_cur);
	return ret;
}

static int oplus_vooc_set_current_when_up_setting_batt_temp
		(struct oplus_vooc_chip *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_INIT) {
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL;
		chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
	}

	switch (chip->vooc_temp_cur_range) {
		case FASTCHG_TEMP_RANGE_NORMAL:
			ret = oplus_vooc_set_current_2_temp_normal_range(chip, vbat_temp_cur);
			break;
		case FASTCHG_TEMP_RANGE_LITTLE_COOL:
			ret = oplus_vooc_set_current_temp_little_cool_range(chip, vbat_temp_cur);
			break;
		case FASTCHG_TEMP_RANGE_COOL:
			ret = oplus_vooc_set_current_temp_cool_range(chip, vbat_temp_cur);
			break;
		default:
			break;
	}

	vooc_xlog_printk(CHG_LOG_CRTI, "the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n", 
			ret, vbat_temp_cur, chip->fastchg_batt_temp_status, chip->vooc_temp_cur_range);
	return ret;
}

int oplus_vooc_get_smaller_battemp_cooldown(int ret_batt, int ret_cool){
	int ret_batt_current =0;
	int ret_cool_current = 0;
	int i = 0;
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if( ret_batt > 0 && ret_batt < ARRAY_SIZE(multistepCurrent)+1
		&& ret_cool > 0 && ret_cool < ARRAY_SIZE(multistepCurrent)+1){
		ret_batt_current =  multistepCurrent[ret_batt -1];
		ret_cool_current = multistepCurrent[ret_cool -1];
		oplus_chg_debug_get_cooldown_current(ret_batt_current, ret_cool_current);
		ret_cool_current = ret_cool_current < ret_batt_current ? ret_cool_current : ret_batt_current;
		if(ret_cool > 0) {
			if(ret_cool_current < ret_batt_current) {
				/*set flag cool down by user */
				oplus_chg_debug_set_cool_down_by_user(1);
			} else {
				/*clear flag cool down by user */
				oplus_chg_debug_set_cool_down_by_user(0);
			}
		}
		for(i = 0 ; i < ARRAY_SIZE(multistepCurrent); i++){
			if(multistepCurrent[i] == ret_cool_current) {
				if(chip) {
					chip->vooc_chg_current_now = ret_cool_current;
				}
				return i + 1;
			}
		}
	}

	return -1;
}


static void oplus_vooc_fastchg_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_vooc_chip *chip = container_of(dwork, struct oplus_vooc_chip, fastchg_work);
	int i = 0;
	int bit = 0;
	int data = 0;
	int ret_info = 0;
	int ret_info_temp = 0;
	int ret_rst = 0;
	static int pre_ret_info = 0;
	static int select_func_flag = 0;
	static bool first_detect_batt_temp = false;
	static bool isnot_power_on = true;
	static bool fw_ver_info = false;
	static bool adapter_fw_ver_info = false;
	static bool data_err = false;
	static bool adapter_model_factory = false;
	int volt = 0;
	int temp = 0;
	int soc = 0;
	int current_now = 0;
	int chg_vol = 0;
	int remain_cap = 0;
	static bool phone_mcu_updated = false;
	static bool normalchg_disabled = false;
/*
	if (!g_adapter_chip) {
		chg_err(" g_adapter_chip NULL\n");
		return;
	}
*/
	usleep_range(2000, 2000);
	if (chip->vops->get_gpio_ap_data(chip) != 1) {
		/*vooc_xlog_printk(CHG_LOG_CRTI, "  Shield fastchg irq, return\r\n");*/
		return;
	}

	chip->vops->eint_unregist(chip);
	for (i = 0; i < 7; i++) {
		bit = chip->vops->read_ap_data(chip);
		data |= bit << (6-i);
		if ((i == 2) && (data != 0x50) && (!fw_ver_info)
				&& (!adapter_fw_ver_info) && (!adapter_model_factory)) {	/*data recvd not start from "101"*/
			vooc_xlog_printk(CHG_LOG_CRTI, "  data err:0x%x\n", data);
			if (chip->fastchg_started == true) {
				chip->allow_reading = true;
				chip->fastchg_started = false;
				chip->fastchg_to_normal = false;
				chip->fastchg_to_warm = false;
				chip->fastchg_ing = false;
				adapter_fw_ver_info = false;
				/*chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;*/
				/*chip->adapter_update_report = chip->adapter_update_real;*/
				chip->btb_temp_over = false;
				oplus_set_fg_i2c_err_occured(false);
				oplus_chg_set_chargerid_switch_val(0);
				chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
				data_err = true;
				if (chip->fastchg_dummy_started) {
					chg_vol = oplus_chg_get_charger_voltage();
					if (chg_vol >= 0 && chg_vol < 2000) {
						chip->fastchg_dummy_started = false;
						oplus_chg_clear_chargerid_info();
						vooc_xlog_printk(CHG_LOG_CRTI,
							"chg_vol:%d dummy_started:false\n", chg_vol);
					}
				} else {
					oplus_chg_clear_chargerid_info();
				}
				///del_timer(&chip->watchdog);
				oplus_vooc_del_watchdog_timer(chip);
			}
			oplus_vooc_set_awake(chip, false);
			goto out;
		}
	}
	vooc_xlog_printk(CHG_LOG_CRTI, " recv data:0x%x, ap:0x%x, mcu:0x%x\n",
		data, chip->fw_data_version, chip->fw_mcu_version);

	if (data == VOOC_NOTIFY_FAST_PRESENT) {
		oplus_vooc_set_awake(chip, true);
		oplus_set_fg_i2c_err_occured(false);
		chip->need_to_up = 0;
		fw_ver_info = false;
		pre_ret_info = 0x06;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		data_err = false;
		phone_mcu_updated = false;
		normalchg_disabled = false;
		first_detect_batt_temp = true;
		chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		if (chip->adapter_update_real == ADAPTER_FW_UPDATE_FAIL) {
			chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
			chip->adapter_update_report = chip->adapter_update_real;
		}
		if (oplus_vooc_get_fastchg_allow() == true) {
			oplus_chg_set_input_current_without_aicl(1200);
			chip->allow_reading = false;
			chip->fastchg_started = true;
			chip->fastchg_ing = false;
			chip->fastchg_dummy_started = false;
			chip->fastchg_to_warm = false;
			chip->btb_temp_over = false;
		} else {
			chip->allow_reading = true;
			chip->fastchg_dummy_started = true;
			chip->fastchg_started = false;
			chip->fastchg_to_normal = false;
			chip->fastchg_to_warm = false;
			chip->fastchg_ing = false;
			chip->btb_temp_over = false;
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		}
		//mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
		if (!isnot_power_on) {
			isnot_power_on = true;
			ret_info = 0x1;
		} else {
			ret_info = 0x2;
		}
	} else if (data == VOOC_NOTIFY_FAST_ABSENT) {
		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		chip->btb_temp_over = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		oplus_set_fg_i2c_err_occured(false);
		if (chip->fastchg_dummy_started) {
			chg_vol = oplus_chg_get_charger_voltage();
			if (chg_vol >= 0 && chg_vol < 2000) {
				chip->fastchg_dummy_started = false;
				chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
				oplus_chg_clear_chargerid_info();
				vooc_xlog_printk(CHG_LOG_CRTI,
					"chg_vol:%d dummy_started:false\n", chg_vol);
			}
		} else {
			chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
			oplus_chg_clear_chargerid_info();
		}
		vooc_xlog_printk(CHG_LOG_CRTI,
			"fastchg stop unexpectly, switch off fastchg\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == VOOC_NOTIFY_ADAPTER_MODEL_FACTORY) {
		vooc_xlog_printk(CHG_LOG_CRTI, " VOOC_NOTIFY_ADAPTER_MODEL_FACTORY!\n");
		/*ready to get adapter_model_factory*/
		adapter_model_factory = 1;
		ret_info = 0x2;
	} else if (adapter_model_factory) {
		vooc_xlog_printk(CHG_LOG_CRTI, "VOOC_NOTIFY_ADAPTER_MODEL_FACTORY:0x%x, \n", data);
		//chip->fast_chg_type = data;
		if (data == 0) {
			chip->fast_chg_type = CHARGER_SUBTYPE_FASTCHG_VOOC;
		} else {
			chip->fast_chg_type = oplus_vooc_convert_fast_chg_type(data);
		}
		adapter_model_factory = 0;
		if (chip->fast_chg_type == 0x0F
				|| chip->fast_chg_type == 0x1F
				|| chip->fast_chg_type == 0x3F
				|| chip->fast_chg_type == 0x7F) {
			chip->allow_reading = true;
			chip->fastchg_started = false;
			chip->fastchg_to_normal = false;
			chip->fastchg_to_warm = false;
			chip->fastchg_ing = false;
			chip->btb_temp_over = false;
			adapter_fw_ver_info = false;
			oplus_set_fg_i2c_err_occured(false);
			oplus_chg_set_chargerid_switch_val(0);
			chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
			data_err = true;
		}
	ret_info = 0x2;
	} else if (data == VOOC_NOTIFY_ALLOW_READING_IIC) {
		chip->fastchg_ing = true;
		chip->allow_reading = true;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		soc = oplus_gauge_get_batt_soc();
		if (oplus_get_fg_i2c_err_occured() == false) {
			volt = oplus_gauge_get_batt_mvolts();
		}
		if (oplus_get_fg_i2c_err_occured() == false) {
			temp = oplus_gauge_get_batt_temperature();
		}
		if (oplus_get_fg_i2c_err_occured() == false) {
			current_now = oplus_gauge_get_batt_current();
		}
		if (oplus_get_fg_i2c_err_occured() == false) {
			remain_cap = oplus_gauge_get_remaining_capacity();
			oplus_gauge_get_batt_fcc();
			oplus_gauge_get_batt_fc();
			oplus_gauge_get_batt_qm();
			oplus_gauge_get_batt_pd();
			oplus_gauge_get_batt_rcu();
			oplus_gauge_get_batt_rcf();
			oplus_gauge_get_batt_fcu();
			oplus_gauge_get_batt_fcf();
			oplus_gauge_get_batt_sou();
			oplus_gauge_get_batt_do0();
			oplus_gauge_get_batt_doe();
			oplus_gauge_get_batt_trm();
			oplus_gauge_get_batt_pc();
			oplus_gauge_get_batt_qs();
		}
		oplus_chg_kick_wdt();
		if (chip->support_vooc_by_normal_charger_path) {//65w
			if(!normalchg_disabled && chip->fast_chg_type != FASTCHG_CHARGER_TYPE_UNKOWN
				&& chip->fast_chg_type != CHARGER_SUBTYPE_FASTCHG_VOOC) {
				oplus_chg_disable_charge();
				normalchg_disabled = true;
			}
		} else {
			if(!normalchg_disabled) {
				oplus_chg_disable_charge();
				normalchg_disabled = true;
			}
		}
		//don't read
		chip->allow_reading = false;
		vooc_xlog_printk(CHG_LOG_CRTI, " volt:%d,temp:%d,soc:%d,current_now:%d,rm:%d, i2c_err:%d\n",
			volt, temp, soc, current_now, remain_cap, oplus_get_fg_i2c_err_occured());
		//mod_timer(&chip->watchdog, jiffies+msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
		if (chip->disable_adapter_output == true) {
			ret_info = (chip->vooc_multistep_adjust_current_support
				&& (!(chip->support_vooc_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC)))
				? 0x07 : 0x03;
		} else if (chip->set_vooc_current_limit == VOOC_MAX_CURRENT_LIMIT_2A
				|| (!(chip->support_vooc_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC)
				&& oplus_chg_get_cool_down_status() >= 1)) {
				ret_info = oplus_chg_get_cool_down_status();
				ret_info = (chip->vooc_multistep_adjust_current_support
				&& (!(chip->support_vooc_by_normal_charger_path
				&& chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC)))
				? ret_info : 0x01;
				vooc_xlog_printk(CHG_LOG_CRTI, "ret_info:%d\n", ret_info);
		} else {
			ret_info = (chip->vooc_multistep_adjust_current_support
				&& (!(chip->support_vooc_by_normal_charger_path
				&&  chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC)))
				? 0x06 : 0x02;
		}

		if (chip->vooc_multistep_adjust_current_support
				&& chip->disable_adapter_output == false
				&& (!(chip->support_vooc_by_normal_charger_path
				&&  chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC))) {
			if (first_detect_batt_temp) {
				if (temp < chip->vooc_multistep_initial_batt_temp) {
					select_func_flag = 1;
				} else {
					select_func_flag = 2;
				}
				first_detect_batt_temp = false;
			}
			if (select_func_flag == 1) {
				ret_info_temp = oplus_vooc_set_current_when_bleow_setting_batt_temp(chip, temp);
			} else {
				ret_info_temp = oplus_vooc_set_current_when_up_setting_batt_temp(chip, temp);
			}
			ret_rst = oplus_vooc_get_smaller_battemp_cooldown(ret_info_temp , ret_info);
			if(ret_rst > 0)
				ret_info = ret_rst;
		}

		if ((chip->vooc_multistep_adjust_current_support == true) && (soc > 75)) {
			ret_rst = oplus_vooc_get_smaller_battemp_cooldown(pre_ret_info , ret_info);
			if(ret_rst > 0)
				ret_info = ret_rst;
		}
		pre_ret_info = ret_info;

		vooc_xlog_printk(CHG_LOG_CRTI, " volt:%d,temp:%d,soc:%d,current_now:%d,rm:%d, i2c_err:%d, ret_info:%d\n",
			volt, temp, soc, current_now, remain_cap, oplus_get_fg_i2c_err_occured(), ret_info);
	} else if (data == VOOC_NOTIFY_NORMAL_TEMP_FULL) {
		vooc_xlog_printk(CHG_LOG_CRTI, "VOOC_NOTIFY_NORMAL_TEMP_FULL\r\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == VOOC_NOTIFY_LOW_TEMP_FULL) {
		vooc_xlog_printk(CHG_LOG_CRTI,
			" fastchg low temp full, switch NORMAL_CHARGER_MODE\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == VOOC_NOTIFY_BAD_CONNECTED) {
		vooc_xlog_printk(CHG_LOG_CRTI,
			" fastchg bad connected, switch NORMAL_CHARGER_MODE\n");
		/*usb bad connected, stop fastchg*/
		chip->btb_temp_over = false;	/*to switch to normal mode*/
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		ret_info = 0x2;
		charger_abnormal_log = CRITICAL_LOG_VOOC_BAD_CONNECTED;
	} else if (data == VOOC_NOTIFY_TEMP_OVER) {
		/*fastchg temp over 45 or under 20*/
		vooc_xlog_printk(CHG_LOG_CRTI,
			" fastchg temp > 45 or < 20, switch NORMAL_CHARGER_MODE\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		ret_info = 0x2;
	} else if (data == VOOC_NOTIFY_BTB_TEMP_OVER) {
		vooc_xlog_printk(CHG_LOG_CRTI, "  btb_temp_over\n");
		chip->fastchg_ing = false;
		chip->btb_temp_over = true;
		chip->fastchg_dummy_started = false;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_to_warm = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
		ret_info = 0x2;
		charger_abnormal_log = CRITICAL_LOG_VOOC_BTB;
	} else if (data == VOOC_NOTIFY_FIRMWARE_UPDATE) {
		vooc_xlog_printk(CHG_LOG_CRTI, " firmware update, get fw_ver ready!\n");
		/*ready to get fw_ver*/
		fw_ver_info = 1;
		ret_info = 0x2;
	} else if (fw_ver_info && chip->firmware_data != NULL) {
		/*get fw_ver*/
		/*fw in local is large than mcu1503_fw_ver*/
		if (!chip->have_updated
				&& chip->firmware_data[chip->fw_data_count - 4] != data) {
			ret_info = 0x2;
			chip->need_to_up = 1;	/*need to update fw*/
			isnot_power_on = false;
		} else {
			ret_info = 0x1;
			chip->need_to_up = 0;	/*fw is already new, needn't to up*/
			adapter_fw_ver_info = true;
		}
		vooc_xlog_printk(CHG_LOG_CRTI, "local_fw:0x%x, need_to_up_fw:%d\n",
			chip->firmware_data[chip->fw_data_count - 4], chip->need_to_up);
		fw_ver_info = 0;
	} else if (adapter_fw_ver_info) {
#if 0
		if (g_adapter_chip->adapter_firmware_data[g_adapter_chip->adapter_fw_data_count - 4] > data
			&& (oplus_gauge_get_batt_soc() > 2) && (chip->vops->is_power_off_charging(chip) == false)
			&& (chip->adapter_update_real != ADAPTER_FW_UPDATE_SUCCESS)) {
#else
		if (0) {
#endif
			ret_info = 0x02;
			chip->adapter_update_real = ADAPTER_FW_NEED_UPDATE;
			chip->adapter_update_report = chip->adapter_update_real;
		} else {
			ret_info = 0x01;
			chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
			chip->adapter_update_report = chip->adapter_update_real;
		}
		adapter_fw_ver_info = false;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
	} else if (data == VOOC_NOTIFY_ADAPTER_FW_UPDATE) {
		oplus_vooc_set_awake(chip, true);
		ret_info = 0x02;
		chip->adapter_update_real = ADAPTER_FW_NEED_UPDATE;
		chip->adapter_update_report = chip->adapter_update_real;
		//mod_timer(&chip->watchdog,  jiffies + msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
	} else {
		oplus_chg_set_chargerid_switch_val(0);
		oplus_chg_clear_chargerid_info();
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		chip->vops->reset_mcu(chip);
		msleep(100);	/*avoid i2c conflict*/
		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		chip->btb_temp_over = false;
		adapter_fw_ver_info = false;
		adapter_model_factory = false;
		data_err = true;
		vooc_xlog_printk(CHG_LOG_CRTI,
			" data err, set 0x101, data=0x%x switch off fastchg\n", data);
		goto out;
	}

	if (chip->fastchg_batt_temp_status == BAT_TEMP_EXIT) {
		vooc_xlog_printk(CHG_LOG_CRTI, "The temperature is lower than 12 du during the fast charging process\n");
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		del_timer(&chip->watchdog);
	}

	msleep(2);
	chip->vops->set_data_sleep(chip);
	if (chip->vooc_multistep_adjust_current_support == false) {
		chip->vops->reply_mcu_data(chip, ret_info,
			oplus_gauge_get_device_type_for_vooc());
	} else {
		chip->vops->reply_mcu_data_4bits(chip, ret_info,
			oplus_gauge_get_device_type_for_vooc());
	}

out:
	chip->vops->set_data_active(chip);
	chip->vops->set_clock_active(chip);
	usleep_range(10000, 10000);
	chip->vops->set_clock_sleep(chip);
	usleep_range(25000, 25000);
	if (chip->fastchg_batt_temp_status == BAT_TEMP_EXIT ) {
		usleep_range(350000, 350000);
		chip->allow_reading = true;
		chip->fastchg_ing = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_started = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_started = true;
	}
	if (data == VOOC_NOTIFY_NORMAL_TEMP_FULL || data == VOOC_NOTIFY_BAD_CONNECTED) {
		usleep_range(350000, 350000);
		chip->allow_reading = true;
		chip->fastchg_ing = false;
		chip->fastchg_to_normal = true;
		chip->fastchg_started = false;
		chip->fastchg_to_warm = false;
		if (data == VOOC_NOTIFY_BAD_CONNECTED)
			charger_abnormal_log = CRITICAL_LOG_VOOC_BAD_CONNECTED;
	} else if (data == VOOC_NOTIFY_LOW_TEMP_FULL) {
		usleep_range(350000, 350000);
		chip->allow_reading = true;
		chip->fastchg_ing = false;
		chip->fastchg_low_temp_full = true;
		chip->fastchg_to_normal = false;
		chip->fastchg_started = false;
		chip->fastchg_to_warm = false;
	} else if (data == VOOC_NOTIFY_TEMP_OVER) {
		usleep_range(350000, 350000);
		chip->fastchg_ing = false;
		chip->fastchg_to_warm = true;
		chip->allow_reading = true;
		chip->fastchg_low_temp_full = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_started = false;
	}

	if (chip->need_to_up) {
		msleep(500);
		//del_timer(&chip->watchdog);
		chip->vops->fw_update(chip);
		chip->need_to_up = 0;
		phone_mcu_updated = true;
		//mod_timer(&chip->watchdog, jiffies + msecs_to_jiffies(25000));
		oplus_vooc_setup_watchdog_timer(chip, 25000);
	}
	if ((data == VOOC_NOTIFY_FAST_ABSENT || (data_err && !phone_mcu_updated)
			|| data == VOOC_NOTIFY_BTB_TEMP_OVER)
			&& (chip->fastchg_dummy_started == false)) {
		oplus_chg_set_charger_type_unknown();
		oplus_chg_wake_update_work();
	} else if (data == VOOC_NOTIFY_NORMAL_TEMP_FULL
			|| data == VOOC_NOTIFY_TEMP_OVER
			|| data == VOOC_NOTIFY_BAD_CONNECTED
			|| data == VOOC_NOTIFY_LOW_TEMP_FULL
			|| chip->fastchg_batt_temp_status == BAT_TEMP_EXIT) {
		oplus_chg_set_charger_type_unknown();
		oplus_vooc_check_charger_out(chip);
	} else if (data == VOOC_NOTIFY_BTB_TEMP_OVER) {
		oplus_chg_set_charger_type_unknown();
	}

	if (chip->adapter_update_real != ADAPTER_FW_NEED_UPDATE) {
		chip->vops->eint_regist(chip);
	}

	if (chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE) {
		chip->allow_reading = true;
		chip->fastchg_started = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_low_temp_full = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_ing = false;
		//del_timer(&chip->watchdog);
		oplus_vooc_del_watchdog_timer(chip);
		oplus_vooc_battery_update();
		oplus_adapter_fw_update();
		oplus_vooc_set_awake(chip, false);
	} else if ((data == VOOC_NOTIFY_FAST_PRESENT)
			|| (data == VOOC_NOTIFY_ALLOW_READING_IIC)
			|| (data == VOOC_NOTIFY_BTB_TEMP_OVER)) {
		oplus_vooc_battery_update();
	} else if ((data == VOOC_NOTIFY_LOW_TEMP_FULL)
		|| (data == VOOC_NOTIFY_FAST_ABSENT)
		|| (data == VOOC_NOTIFY_NORMAL_TEMP_FULL)
		|| (data == VOOC_NOTIFY_BAD_CONNECTED)
		|| (data == VOOC_NOTIFY_TEMP_OVER)) {
		oplus_vooc_battery_update();
#ifdef CHARGE_PLUG_IN_TP_AVOID_DISTURB
		charge_plug_tp_avoid_distrub(1, is_oplus_fast_charger);
#endif
		oplus_vooc_set_awake(chip, false);
	} else if (data_err) {
		data_err = false;
		oplus_vooc_battery_update();
#ifdef CHARGE_PLUG_IN_TP_AVOID_DISTURB
		charge_plug_tp_avoid_distrub(1, is_oplus_fast_charger);
#endif
		oplus_vooc_set_awake(chip, false);
	}
	if (chip->fastchg_started == false
			&& chip->fastchg_dummy_started == false
			&& chip->fastchg_to_normal == false
			&& chip->fastchg_to_warm == false){
		chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	}

}

void fw_update_thread(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_vooc_chip *chip = container_of(dwork,
		struct oplus_vooc_chip, fw_update_work);
	const struct firmware *fw = NULL;
	int ret = 1;
	int retry = 5;
	char version[10];

	if(chip->vooc_fw_update_newmethod) {
		if(oplus_is_rf_ftm_mode()) {
			chip->vops->fw_check_then_recover(chip);
			return;
		}
		 do {
			ret = request_firmware_select(&fw, chip->fw_path, chip->dev);
			if (!ret) {
				break;
			}
		} while((ret < 0) && (--retry > 0));
		chg_debug(" retry times %d, chip->fw_path[%s]\n", 5 - retry, chip->fw_path);
		if(!ret) {
			chip->firmware_data =  fw->data;
			chip->fw_data_count =  fw->size;
			chip->fw_data_version = chip->firmware_data[chip->fw_data_count - 4];
			chg_debug("count:0x%x, version:0x%x\n",
				chip->fw_data_count,chip->fw_data_version);
			if(chip->vops->fw_check_then_recover) {
				ret = chip->vops->fw_check_then_recover(chip);
				sprintf(version,"%d", chip->fw_data_version);
				sprintf(chip->manufacture_info.version,"%s", version);
				if (ret == FW_CHECK_MODE) {
					chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
					chip->fastchg_dummy_started = false;
					chip->fastchg_started = false;
					del_timer(&chip->watchdog);
					if (chip->vops->reset_mcu)
						chip->vops->reset_mcu(chip);
				}
			}
			release_firmware(fw);
			chip->firmware_data = NULL;
		} else {
			chg_debug("%s: fw_name request failed, %d\n", __func__, ret);
		}
	}else {
		ret = chip->vops->fw_check_then_recover(chip);
		if (ret == FW_CHECK_MODE) {
			chg_debug("update finish, then clean fastchg_dummy , fastchg_started, watch_dog\n");
			chip->fastchg_dummy_started = false;
			chip->fastchg_started = false;
			del_timer(&chip->watchdog);
			if (chip->vops->reset_mcu)
				chip->vops->reset_mcu(chip);
		}
	}
}

#define FASTCHG_FW_INTERVAL_INIT	   1000	/*  1S     */
void oplus_vooc_fw_update_work_init(struct oplus_vooc_chip *chip)
{
	INIT_DELAYED_WORK(&chip->fw_update_work, fw_update_thread);
	schedule_delayed_work(&chip->fw_update_work, round_jiffies_relative(msecs_to_jiffies(FASTCHG_FW_INTERVAL_INIT)));
}

void oplus_vooc_shedule_fastchg_work(void)
{
	if (!g_vooc_chip) {
		chg_err(" g_vooc_chip is NULL\n");
	} else {
		schedule_delayed_work(&g_vooc_chip->fastchg_work, 0);
	}
}
static ssize_t proc_fastchg_fw_update_write(struct file *file, const char __user *buff,
		size_t len, loff_t *data)
{
	struct oplus_vooc_chip *chip = PDE_DATA(file_inode(file));
	char write_data[32] = {0};

	if (copy_from_user(&write_data, buff, len)) {
		chg_err("fastchg_fw_update error.\n");
		return -EFAULT;
	}

	if (write_data[0] == '1') {
		chg_err("fastchg_fw_update\n");
		chip->fw_update_flag = 1;
		schedule_delayed_work(&chip->fw_update_work, 0);
	} else {
		chip->fw_update_flag = 0;
		chg_err("Disable fastchg_fw_update\n");
	}

	return len;
}

static ssize_t proc_fastchg_fw_update_read(struct file *file, char __user *buff,
		size_t count, loff_t *off)
{
	struct oplus_vooc_chip *chip = PDE_DATA(file_inode(file));
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if(chip->fw_update_flag == 1) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page,"%s",read_data);
	if(len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff,page,(len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}


static const struct file_operations fastchg_fw_update_proc_fops = {
	.write = proc_fastchg_fw_update_write,
	.read  = proc_fastchg_fw_update_read,
};

static int init_proc_fastchg_fw_update(struct oplus_vooc_chip *chip)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create_data("fastchg_fw_update", 0664, NULL, &fastchg_fw_update_proc_fops,chip);
	if (!p) {
		pr_err("proc_create fastchg_fw_update_proc_fops fail!\n");
	}
	return 0;
}

static int init_vooc_proc(struct oplus_vooc_chip *chip)
{
	strcpy(chip->manufacture_info.version, "0");
	if (get_vooc_mcu_type(chip) == OPLUS_VOOC_MCU_HWID_STM8S) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw.bin", get_project());
	} else if (get_vooc_mcu_type(chip) == OPLUS_VOOC_MCU_HWID_N76E) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw_n76e.bin", get_project());
	} else if (get_vooc_mcu_type(chip) == OPLUS_VOOC_ASIC_HWID_RK826) {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw_rk826.bin", get_project());
	} else {
		snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw_op10.bin", get_project());
	}
	memcpy(chip->manufacture_info.manufacture, chip->fw_path, MAX_FW_NAME_LENGTH);
	register_devinfo("fastchg", &chip->manufacture_info);
	init_proc_fastchg_fw_update(chip);
	chg_debug(" version:%s, fw_path:%s\n", chip->manufacture_info.version, chip->fw_path);
	return 0;
}
void oplus_vooc_init(struct oplus_vooc_chip *chip)
{
	int ret = 0;

	chip->allow_reading = true;
	chip->fastchg_started = false;
	chip->fastchg_dummy_started = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_allow = false;
	chip->fastchg_low_temp_full = false;
	chip->have_updated = false;
	chip->need_to_up = false;
	chip->btb_temp_over = false;
	chip->adapter_update_real = ADAPTER_FW_UPDATE_NONE;
	chip->adapter_update_report = chip->adapter_update_real;
	chip->mcu_update_ing = false;
	chip->mcu_boot_by_gpio = false;
	chip->dpdm_switch_mode = NORMAL_CHARGER_MODE;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	/*chip->batt_psy = power_supply_get_by_name("battery");*/
	chip->disable_adapter_output = false;
	chip->set_vooc_current_limit = VOOC_MAX_CURRENT_NO_LIMIT;
	oplus_vooc_init_watchdog_timer(chip);
	oplus_vooc_awake_init(chip);
	INIT_DELAYED_WORK(&chip->fastchg_work, oplus_vooc_fastchg_func);
	INIT_DELAYED_WORK(&chip->check_charger_out_work, check_charger_out_work_func);
	INIT_WORK(&chip->vooc_watchdog_work, vooc_watchdog_work_func);
	g_vooc_chip = chip;
	chip->vops->eint_regist(chip);
	if(chip->vooc_fw_update_newmethod) {
		if(oplus_is_rf_ftm_mode()) {
			return;
		}
		INIT_DELAYED_WORK(&chip->fw_update_work, fw_update_thread);
		//Alloc fw_name/devinfo memory space

		chip->fw_path = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
		if (chip->fw_path == NULL) {
			ret = -ENOMEM;
			chg_err("panel_data.fw_name kzalloc error\n");
			goto manu_fwpath_alloc_err;
		}
		chip->manufacture_info.version = kzalloc(MAX_DEVICE_VERSION_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.version == NULL) {
			ret = -ENOMEM;
			chg_err("manufacture_info.version kzalloc error\n");
			goto manu_version_alloc_err;
		}
		chip->manufacture_info.manufacture = kzalloc(MAX_DEVICE_MANU_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.manufacture == NULL) {
			ret = -ENOMEM;
			chg_err("panel_data.manufacture kzalloc error\n");
			goto manu_info_alloc_err;
		}
		init_vooc_proc(chip);
		return;

manu_fwpath_alloc_err:
		kfree(chip->fw_path);

manu_info_alloc_err:
		kfree(chip->manufacture_info.manufacture);

manu_version_alloc_err:
		kfree(chip->manufacture_info.version);
	}
	return ;
}

bool oplus_vooc_wake_fastchg_work(struct oplus_vooc_chip *chip)
{
	return schedule_delayed_work(&chip->fastchg_work, 0);
}

void oplus_vooc_print_log(void)
{
	if (!g_vooc_chip) {
		return;
	}
	vooc_xlog_printk(CHG_LOG_CRTI, "VOOC[ %d / %d / %d / %d / %d / %d]\n",
		g_vooc_chip->fastchg_allow, g_vooc_chip->fastchg_started, g_vooc_chip->fastchg_dummy_started,
		g_vooc_chip->fastchg_to_normal, g_vooc_chip->fastchg_to_warm, g_vooc_chip->btb_temp_over);
}

bool oplus_vooc_get_allow_reading(void)
{
	if (!g_vooc_chip) {
		return true;
	} else {
		if (g_vooc_chip->support_vooc_by_normal_charger_path
				&& g_vooc_chip->fast_chg_type == CHARGER_SUBTYPE_FASTCHG_VOOC) {
			return true;
		} else {
			return g_vooc_chip->allow_reading;
		}
	}
}

bool oplus_vooc_get_fastchg_started(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_started;
	}
}

bool oplus_vooc_get_fastchg_ing(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_ing;
	}
}

bool oplus_vooc_get_fastchg_allow(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_allow;
	}
}

void oplus_vooc_set_fastchg_allow(int enable)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fastchg_allow = enable;
	}
}

bool oplus_vooc_get_fastchg_to_normal(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_to_normal;
	}
}

void oplus_vooc_set_fastchg_to_normal_false(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fastchg_to_normal = false;
	}
}

void oplus_vooc_set_fastchg_type_unknow(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	}
}

bool oplus_vooc_get_fastchg_to_warm(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_to_warm;
	}
}

void oplus_vooc_set_fastchg_to_warm_false(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fastchg_to_warm = false;
	}
}

bool oplus_vooc_get_fastchg_low_temp_full()
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_low_temp_full;
	}
}

void oplus_vooc_set_fastchg_low_temp_full_false(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fastchg_low_temp_full = false;
	}
}

bool oplus_vooc_get_vooc_multistep_adjust_current_support(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->vooc_multistep_adjust_current_support;
	}
}

bool oplus_vooc_get_fastchg_dummy_started(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->fastchg_dummy_started;
	}
}

void oplus_vooc_set_fastchg_dummy_started_false(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->fastchg_dummy_started = false;
	}
}

int oplus_vooc_get_adapter_update_status(void)
{
	if (!g_vooc_chip) {
		return ADAPTER_FW_UPDATE_NONE;
	} else {
		return g_vooc_chip->adapter_update_report;
	}
}

int oplus_vooc_get_adapter_update_real_status(void)
{
	if (!g_vooc_chip) {
		return ADAPTER_FW_UPDATE_NONE;
	} else {
		return g_vooc_chip->adapter_update_real;
	}
}

bool oplus_vooc_get_btb_temp_over(void)
{
	if (!g_vooc_chip) {
		return false;
	} else {
		return g_vooc_chip->btb_temp_over;
	}
}

void oplus_vooc_reset_fastchg_after_usbout(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->vops->reset_fastchg_after_usbout(g_vooc_chip);
	}
}

void oplus_vooc_switch_fast_chg(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->vops->switch_fast_chg(g_vooc_chip);
	}
}

void oplus_vooc_set_ap_clk_high(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->vops->set_clock_sleep(g_vooc_chip);
	}
}

void oplus_vooc_reset_mcu(void)
{
	if (!g_vooc_chip) {
		return;
	} else {
		g_vooc_chip->vops->reset_mcu(g_vooc_chip);
	}
}

bool oplus_vooc_check_chip_is_null(void)
{
	if (!g_vooc_chip) {
		return true;
	} else {
		return false;
	}
}

int oplus_vooc_get_vooc_switch_val(void)
{
	if (!g_vooc_chip) {
		return 0;
	} else {
		return g_vooc_chip->vops->get_switch_gpio_val(g_vooc_chip);
	}
}

void oplus_vooc_uart_init(void)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->vops->set_data_active(chip);
		chip->vops->set_clock_sleep(chip);
	}
}

int oplus_vooc_get_uart_tx(void)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return -1;
	} else {
		return chip->vops->get_clk_gpio_num(chip);
	}
}

int oplus_vooc_get_uart_rx(void)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return -1;
	} else {
		return chip->vops->get_data_gpio_num(chip);
	}
}


void oplus_vooc_uart_reset(void)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->vops->eint_regist(chip);
		oplus_chg_set_chargerid_switch_val(0);
		chip->vops->set_switch_mode(chip, NORMAL_CHARGER_MODE);
		chip->vops->reset_mcu(chip);
	}
}

void oplus_vooc_set_adapter_update_real_status(int real)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->adapter_update_real = real;
	}
}

void oplus_vooc_set_adapter_update_report_status(int report)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->adapter_update_report = report;
	}
}

int oplus_vooc_get_fast_chg_type(void)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return FASTCHG_CHARGER_TYPE_UNKOWN;
	} else {
		return chip->fast_chg_type;
	}
}

static int oplus_vooc_convert_fast_chg_type(int fast_chg_type)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	enum e_fastchg_power fastchg_pwr_type;

	if (!chip)
		return FASTCHG_CHARGER_TYPE_UNKOWN;

	if (chip->support_vooc_by_normal_charger_path) {
		fastchg_pwr_type = FASTCHG_POWER_10V6P5A_TWO_BAT_SVOOC;
	} else {
		fastchg_pwr_type = FASTCHG_POWER_UNKOWN;
	}

	switch(fast_chg_type) {
	case FASTCHG_CHARGER_TYPE_UNKOWN:
		return fast_chg_type;
		break;

	case 0x11:		/*50w*/
	case 0x12:		/*50w*/
	case 0x21:		/*50w*/
	case 0x31:		/*50w*/
	case 0x33:		/*50w*/
	case 0x62:		/*reserve for svooc*/
		if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
				|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SVOOC
				|| fastchg_pwr_type == FASTCHG_POWER_10V5A_SINGLE_BAT_SVOOC)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_VOOC;
		break;

	case 0x14:		/*65w*/
	case 0x32:		/*65W*/
	case 0x35:		/*65w*/
	case 0x36:		/*65w*/
	case 0x63:		/*reserve for svooc 2.0*/
	case 0x64:		/*reserve for svooc 2.0*/
	case 0x65:		/*reserve for svooc 2.0*/
	case 0x66:		/*reserve for svooc 2.0*/
	case 0x69:		/*reserve for svooc 2.0*/
	case 0x6A:		/*reserve for svooc 2.0*/
	case 0x6B:		/*reserve for svooc 2.0*/
	case 0x6C:		/*reserve for svooc 2.0*/
	case 0x6D:		/*reserve for svooc 2.0*/
	case 0x6E:		/*reserve for svooc 2.0*/
		if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
				|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SVOOC
				|| fastchg_pwr_type == FASTCHG_POWER_10V5A_SINGLE_BAT_SVOOC)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_VOOC;
		break;

	case 0x0F:		/*special code*/
	case 0x1F:		/*special code*/
	case 0x3F:		/*special code*/
	case 0x7F:		/*special code*/
		return fast_chg_type;
		break;

	case 0x34:
		if (fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SVOOC
				|| fastchg_pwr_type == FASTCHG_POWER_10V5A_SINGLE_BAT_SVOOC)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_VOOC;
	case 0x13:
	case 0x19:
	case 0x29:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
		return CHARGER_SUBTYPE_FASTCHG_VOOC;
	case 0x61:		/* 11V3A*/
	case 0x49:		/*for 11V3A adapter temp*/
	case 0x4A:		/*for 11V3A adapter temp*/
	case 0x4B:		/*for 11V3A adapter temp*/
	case 0x4C:		/*for 11V3A adapter temp*/
	case 0x4D:		/*for 11V3A adapter temp*/
	case 0x4E:		/*for 11V3A adapter temp*/
		fast_chg_type = 0x61;
		if (fastchg_pwr_type == FASTCHG_POWER_11V3A_FLASHCHARGER
				|| fastchg_pwr_type == FASTCHG_POWER_10V6P5A_TWO_BAT_SVOOC
				|| fastchg_pwr_type == FASTCHG_POWER_10V5A_SINGLE_BAT_SVOOC)
			return fast_chg_type;
		return CHARGER_SUBTYPE_FASTCHG_VOOC;

	default:
		return CHARGER_SUBTYPE_FASTCHG_SVOOC;
	}

	return FASTCHG_CHARGER_TYPE_UNKOWN;
}

void oplus_vooc_set_disable_adapter_output(bool disable)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->disable_adapter_output = disable;
	}
	chg_err(" chip->disable_adapter_output:%d\n", chip->disable_adapter_output);
}

void oplus_vooc_set_vooc_max_current_limit(int current_level)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;
	if (!chip) {
		return ;
	} else {
		chip->set_vooc_current_limit = current_level;
	}
}

void oplus_vooc_set_vooc_chargerid_switch_val(int value)
{
	struct oplus_vooc_chip *chip = g_vooc_chip;

	if (!chip) {
		return;
	} else if (chip->vops->set_vooc_chargerid_switch_val) {
		chip->vops->set_vooc_chargerid_switch_val(chip, value);
	}
}
void oplus_vooc_get_vooc_chip_handle(struct oplus_vooc_chip **chip) {
	*chip = g_vooc_chip;
}
