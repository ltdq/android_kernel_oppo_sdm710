/***************************************************************
** Copyright (C),  2018,  OPPO Mobile Comm Corp.,  Ltd
** OPLUS_BUG_STABILITY
** File : dsi_oppo_support.c
** Description : display driver private management
** Version : 1.0
** Date : 2018/03/17
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Hu.Jie          2018/03/17        1.0           Build this moudle
******************************************************************/
#include <linux/dsi_oppo_support.h>
#include <soc/oppo/boot_mode.h>
#include <soc/oplus/system/oppo_project.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/boot_mode.h>
#include <linux/notifier.h>

static enum oppo_display_support_list  oppo_display_vendor = OPPO_17107_SAMSUNG_DISPLAY_FHD_DSC_CMD_PANEL;
static enum oppo_display_power_status oppo_display_status = OPPO_DISPLAY_POWER_OFF;
static enum oppo_display_scene oppo_siaplay_save_scene = OPPO_DISPLAY_NORMAL_SCENE;

static BLOCKING_NOTIFIER_HEAD(oppo_display_notifier_list);

int oppo_display_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oppo_display_notifier_list,
						nb);
}
EXPORT_SYMBOL(oppo_display_register_client);


int oppo_display_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oppo_display_notifier_list,
						  nb);
}
EXPORT_SYMBOL(oppo_display_unregister_client);

static int oppo_display_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&oppo_display_notifier_list, val,
					    v);
}

bool is_oppo_correct_display(enum oppo_display_support_list lcd_name) {
	return (oppo_display_vendor == lcd_name ? true:false);
}

bool is_silence_reboot(void) {
	if((MSM_BOOT_MODE__SILENCE == get_boot_mode()) || (MSM_BOOT_MODE__SAU == get_boot_mode())) {
		return true;
	} else {
		return false;
	}
}

int set_oppo_display_vendor(const char * display_name) {
	if (display_name == NULL)
		return -1;

	if (!strcmp(display_name,"dsi_oppo17107_samsung_sofeg02_fhd_dsc_cmd_display")) {
		oppo_display_vendor = OPPO_17107_SAMSUNG_DISPLAY_FHD_DSC_CMD_PANEL;
		register_device_proc("lcd", "SOFEF00_M-QC01", "samsung1024");
	} else if(!strcmp(display_name,"dsi_oppo17107_samsung_sofeg01_s_1080p_cmd_display")) {
		oppo_display_vendor = OPPO_17021_SAMSUNG_DISPLAY_FHD_CMD_PANEL;
		register_device_proc("lcd", "SOFEG01_S-QC01", "samsung1024");
	} else if(!strcmp(display_name,"dsi_oppo17081samsung_ams596w401_1080_2280_cmd_display")) {
		oppo_display_vendor = OPPO_17081_SAMSUNG_DISPLAY_FHD_CMD_PANEL;
		register_device_proc("lcd", "AMS596W401", "samsung1024");
	} else if(!strcmp(display_name,"dsi_oppo18081samsung_ams641rw01_1080_2340_cmd_display")) {
		oppo_display_vendor = OPPO_18081_SAMSUNG_DISPLAY_FHD_CMD_PANEL;
		register_device_proc("lcd", "AMS641RW01", "samsung1024");
	} else if(!strcmp(display_name,"dsi_oppo18041samsung_ams653tk01_1080_2340_cmd_display") ||
			  !strcmp(display_name,"dsi_oppo18041samsung_ams653tk01_1080_2340_cmd_display_evt")){
		oppo_display_vendor = OPPO_18041_SAMSUNG_DISPLAY_FHD_CMD_PANEL;
		register_device_proc("lcd", "AMS653TK01", "samsung1024");
	} else if(!strcmp(display_name,"dsi_rm19651samsung_ams641rw01_1080_2340_cmd_display")) {
		oppo_display_vendor = OPPO_19651_SAMSUNG_DISPLAY_FHD_CMD_PANEL;
		register_device_proc("lcd", "AMS641RW01", "samsung1024");
	} else if(!strcmp(display_name,"dsi_oppo18621dsjm_hx83112a_1080_2340_video_display")) {
                oppo_display_vendor = OPPO_18621_DSJM_DISPLAY_FHD_VIDEO_PANEL;
		if (strstr(saved_command_line,"2str")) {
			register_device_proc("lcd", "HX83112A", "dsjm4096_2str vdo mode");
		} else {
			register_device_proc("lcd", "HX83112A", "dsjm4096 vdo mode");
		}
	} else {
		oppo_display_vendor = OPPO_DISPLAY_UNKNOW;
	}

	return 0;
}

void notifier_oppo_display_early_status(enum oppo_display_power_status power_status) {
	int blank;
	OPPO_DISPLAY_NOTIFIER_EVENT oppo_notifier_data;
	switch (power_status) {
		case OPPO_DISPLAY_POWER_ON:
			blank = OPPO_DISPLAY_POWER_ON;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_ON;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EARLY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_DOZE:
			blank = OPPO_DISPLAY_POWER_DOZE;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_DOZE;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EARLY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_DOZE_SUSPEND:
			blank = OPPO_DISPLAY_POWER_DOZE_SUSPEND;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_DOZE_SUSPEND;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EARLY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_OFF:
			blank = OPPO_DISPLAY_POWER_OFF;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_OFF;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EARLY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		default:
			break;
		}
}

void notifier_oppo_display_status(enum oppo_display_power_status power_status) {
	int blank;
	OPPO_DISPLAY_NOTIFIER_EVENT oppo_notifier_data;
	switch (power_status) {
		case OPPO_DISPLAY_POWER_ON:
			blank = OPPO_DISPLAY_POWER_ON;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_ON;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_DOZE:
			blank = OPPO_DISPLAY_POWER_DOZE;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_DOZE;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_DOZE_SUSPEND:
			blank = OPPO_DISPLAY_POWER_DOZE_SUSPEND;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_DOZE_SUSPEND;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		case OPPO_DISPLAY_POWER_OFF:
			blank = OPPO_DISPLAY_POWER_OFF;
			oppo_notifier_data.data = &blank;
			oppo_notifier_data.status = OPPO_DISPLAY_POWER_OFF;
			oppo_display_notifier_call_chain(OPPO_DISPLAY_EVENT_BLANK,
						     &oppo_notifier_data);
			break;
		default:
			break;
		}
}

void set_oppo_display_power_status(enum oppo_display_power_status power_status) {
	oppo_display_status = power_status;
}

enum oppo_display_power_status get_oppo_display_power_status(void) {
	return oppo_display_status;
}
EXPORT_SYMBOL(get_oppo_display_power_status);

void set_oppo_display_scene(enum oppo_display_scene display_scene) {
	oppo_siaplay_save_scene = display_scene;
}

enum oppo_display_scene get_oppo_display_scene(void) {
	return oppo_siaplay_save_scene;
}

EXPORT_SYMBOL(get_oppo_display_scene);

bool is_oppo_display_support_feature(enum oppo_display_feature feature_name) {
	bool ret = false;
	switch (feature_name) {
		case OPPO_DISPLAY_HDR:
			ret = false;
			break;
		case OPPO_DISPLAY_SEED:
			ret = true;
			break;
		case OPPO_DISPLAY_HBM:
			ret = true;
			break;
		case OPPO_DISPLAY_LBR:
			ret = true;
			break;
		case OPPO_DISPLAY_AOD:
			ret = true;
			break;
		case OPPO_DISPLAY_ULPS:
			ret = false;
			break;
		case OPPO_DISPLAY_ESD_CHECK:
			ret = true;
			break;
		case OPPO_DISPLAY_DYNAMIC_MIPI:
			ret = true;
			break;
		case OPPO_DISPLAY_PARTIAL_UPDATE:
			ret = false;
			break;
		default:
			break;
	}
	return ret;
}


