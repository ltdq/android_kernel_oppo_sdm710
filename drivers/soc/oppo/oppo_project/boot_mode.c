#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <soc/qcom/smem.h>
#include <asm/uaccess.h>

#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <soc/oppo/boot_mode.h>
static struct kobject *systeminfo_kobj;

#define MAX_CMD_LENGTH 32

static int ftm_mode __ro_after_init = MSM_BOOT_MODE__NORMAL;

int __init  board_mfg_mode_init(void)
{
        char *substr;

        substr = strstr(boot_command_line, "oppo_ftm_mode=");
        if (substr) {
                substr += strlen("oppo_ftm_mode=");

                if (strncmp(substr, "factory2", 5) == 0) {
                        ftm_mode = MSM_BOOT_MODE__FACTORY;
                        pr_err("kernel ftm OK\r\n");
                } else if (strncmp(substr, "ftmwifi", 5) == 0) {
                        ftm_mode = MSM_BOOT_MODE__WLAN;
                } else if (strncmp(substr, "ftmmos", 5) == 0) {
                        ftm_mode = MSM_BOOT_MODE__MOS;
                } else if (strncmp(substr, "ftmrf", 5) == 0) {
                        ftm_mode = MSM_BOOT_MODE__RF;
                } else if (strncmp(substr, "ftmrecovery", 5) == 0) {
                        ftm_mode = MSM_BOOT_MODE__RECOVERY;
                } else if (strncmp(substr, "ftmsilence", 10) == 0) {
                        ftm_mode = MSM_BOOT_MODE__SILENCE;
                } else if (strncmp(substr, "ftmsau", 6) == 0) {
                        ftm_mode = MSM_BOOT_MODE__SAU;
                } else if (strncmp(substr, "ftmsafe", 7) == 0) {
                        ftm_mode = MSM_BOOT_MODE__SAFE;
                }
        }

        pr_err("board_mfg_mode_init, " "ftm_mode=%d\n", ftm_mode);
        return 0;
}
/*__setup("oppo_ftm_mode=", board_mfg_mode_init);*/

int get_boot_mode(void)
{
        return ftm_mode;
}
#ifdef VENDOR_EDIT
EXPORT_SYMBOL(get_boot_mode);
#endif /* VENDOR_EDIT */

static ssize_t ftmmode_show(struct kobject *kobj, struct kobj_attribute *attr,
                                 char *buf)
{
        return sprintf(buf, "%d\n", ftm_mode);
}

struct kobj_attribute ftmmode_attr = {
        .attr = {"ftmmode", 0644},

        .show = &ftmmode_show,
};


/* OPPO 2013-01-04 Van add start for ftm close modem*/
/*#define mdm_drv_ap2mdm_pmic_pwr_en_gpio  27 */

static ssize_t closemodem_store(struct kobject *kobj, struct kobj_attribute *attr,
                         const char *buf, size_t count)
{
        /*writing '1' to close and '0' to open*/
        /*pr_err("closemodem buf[0] = 0x%x", buf[0]);*/
        switch (buf[0]) {
        case 0x30:
                break;
        case 0x31:
        /*        pr_err("closemodem now");
*/
        /*        gpio_direction_output(mdm_drv_ap2mdm_pmic_pwr_en_gpio, 0);
*/
                mdelay(4000);
                break;
        default:
                break;
        }

        return count;
}

struct kobj_attribute closemodem_attr = {
        .attr = {"closemodem", 0644},
        /*.show = &closemodem_show, 
*/
        .store = &closemodem_store
};
/* OPPO 2013-01-04 Van add end for ftm close modem*/
static struct attribute * g[] = {
        &ftmmode_attr.attr,
/* OPPO 2013-01-04 Van add start for ftm close modem*/
        &closemodem_attr.attr,
/* OPPO 2013-01-04 Van add end for ftm close modem*/
        NULL,
};

static struct attribute_group attr_group = {
        .attrs = g,
};

#ifdef VENDOR_EDIT
/* OPPO 2013-09-03 heiwei add for add interface start reason and boot_mode begin */
char pwron_event[MAX_CMD_LENGTH + 1];
static int __init start_reason_init(void)
{
        int i;
        char * substr = strstr(boot_command_line, "androidboot.startupmode=");
        if (NULL == substr) {
                return 0;
        }
        substr += strlen("androidboot.startupmode=");
        for (i=0; substr[i] != ' ' && i < MAX_CMD_LENGTH && substr[i] != '\0'; i++) {
                pwron_event[i] = substr[i];
        }
        pwron_event[i] = '\0';
        printk(KERN_INFO "%s: parse poweron reason %s i = %d\n", __func__, pwron_event, i);
        return 1;
}
/*__setup("androidboot.startupmode=", start_reason_setup);*/

char boot_mode[MAX_CMD_LENGTH + 1] __ro_after_init;

#ifdef VENDOR_EDIT
bool qpnp_is_power_off_charging(void)
{
        /*pr_err("%s boot_mode:%s\n", __func__, boot_mode);
*/
        if (!strcmp(boot_mode, "charger")) {
                return true;
        }
        else
                return false;
}
#endif
#ifdef VENDOR_EDIT
#ifdef HANG_OPPO_ALL
bool op_is_monitorable_boot(void)
{
    if (ftm_mode != MSM_BOOT_MODE__NORMAL) {
        return false;
    }

    if (!strcmp(boot_mode, "normal")) {
        return true;
    } else if (!strcmp(boot_mode, "reboot")) {
        return true;
    } else if (!strcmp(boot_mode, "kernel")) {
        return true;
    } else {
        return false;
    }
}
#endif  //HANG_OPPO_ALL
#endif

#ifdef VENDOR_EDIT
char charger_reboot[MAX_CMD_LENGTH + 1];
bool qpnp_is_charger_reboot(void)
{
        pr_err("%s charger_reboot:%s\n", __func__, charger_reboot);
        if (!strcmp(charger_reboot, "1")) {
                return true;
        }
        else
                return false;
}
static int __init oppo_charger_reboot(void)
{
        int i;
        char * substr = strstr(boot_command_line, "oppo_charger_present=");
        if (NULL == substr) {
                return 0;
        }
        substr += strlen("oppo_charger_present=");
        for (i=0; substr[i] != ' '&& i < MAX_CMD_LENGTH && substr[i] != '\0'; i++) {
                charger_reboot[i] = substr[i];
        }
        charger_reboot[i] = '\0';
        printk(KERN_INFO "%s: parse charger_reboot %s\n", __func__, charger_reboot);
        return 1;
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
static int verified_boot_state __ro_after_init = VERIFIED_BOOT_STATE__GREEN;
bool is_bootloader_unlocked(void)
{
        return verified_boot_state == VERIFIED_BOOT_STATE__ORANGE;
}

static int __init verified_boot_state_init(void)
{
        char * substr = strstr(boot_command_line, "androidboot.verifiedbootstate=");
        if (substr) {
                substr += strlen("androidboot.verifiedbootstate=");
                if (strncmp(substr, "green", 5) == 0) {
                        verified_boot_state = VERIFIED_BOOT_STATE__GREEN;
                } else if (strncmp(substr, "orange", 6) == 0) {
                        verified_boot_state = VERIFIED_BOOT_STATE__ORANGE;
                } else if (strncmp(substr, "yellow", 6) == 0) {
                        verified_boot_state = VERIFIED_BOOT_STATE__YELLOW;
                } else if (strncmp(substr, "red", 3) == 0) {
                        verified_boot_state = VERIFIED_BOOT_STATE__RED;
		} else {
			verified_boot_state = VERIFIED_BOOT_STATE__GREEN;
                }
        }
        return 0;
}
#endif /*VENDOR_EDIT*/

int __init  board_boot_mode_init(void)
{
        int i;
        char *substr;

        substr =  strstr(boot_command_line, "androidboot.mode=");
        if (substr) {
                substr += strlen("androidboot.mode=");
                for (i=0; substr[i] != ' ' && i < MAX_CMD_LENGTH && substr[i] != '\0'; i++) {
                        boot_mode[i] = substr[i];
                }
                boot_mode[i] = '\0';
                pr_err("board_boot_mode_init boot_mode=%s\n", boot_mode);
        }
        return 0;
}

static int __init boot_mode_init(void)
{
        int rc = 0;

        pr_err("%s: parse boot_mode\n", __func__);
        board_boot_mode_init();
#ifdef VENDOR_EDIT
        /* OPPO 2013.07.09 hewei add begin for factory mode*/
        board_mfg_mode_init();
        /* OPPO 2013.07.09 hewei add end */
#endif /*VENDOR_EDIT
*/

/* OPPO 2013-09-03 heiwei add for add interface start reason and boot_mode begin */
        start_reason_init();
#ifdef VENDOR_EDIT
        oppo_charger_reboot();
#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
        verified_boot_state_init();
#endif /*VENDOR_EDIT*/

/* OPPO 2013-09-03 zhanglong add for add interface start reason and boot_mode end */
        /* OPPO 2013.07.09 hewei add begin for factory mode*/
        systeminfo_kobj = kobject_create_and_add("systeminfo", NULL);
        printk("songxh create systeminto node suscess!\n");
        if (systeminfo_kobj) {
                rc = sysfs_create_group(systeminfo_kobj, &attr_group);
        }
        /* OPPO 2013.07.09 hewei add end */

        return 1;
}
/*__setup("androidboot.mode=", boot_mode_setup);*/
/* OPPO 2013-09-03 zhanglong add for add interface start reason and boot_mode end */
#endif /*VENDOR_EDIT
*/

/*module_init(boot_mode_init);*/
arch_initcall(boot_mode_init);
