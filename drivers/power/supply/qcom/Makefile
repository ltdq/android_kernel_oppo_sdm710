obj-$(CONFIG_QPNP_FG)		+= qpnp-fg.o
obj-$(CONFIG_QPNP_FG_GEN3)     += qpnp-fg-gen3.o fg-memif.o fg-util.o
obj-$(CONFIG_QPNP_SMBCHARGER)	+= qpnp-smbcharger.o pmic-voter.o
obj-$(CONFIG_SMB135X_CHARGER)   += smb135x-charger.o pmic-voter.o
obj-$(CONFIG_SMB1360_CHARGER_FG) += smb1360-charger-fg.o
obj-$(CONFIG_SMB1355_SLAVE_CHARGER)   += smb1355-charger.o pmic-voter.o
obj-$(CONFIG_SMB1351_USB_CHARGER) += smb1351-charger.o pmic-voter.o battery.o
#ifdef OPLUS_FEATURE_CHG_BASIC
obj-$(CONFIG_QPNP_SMB2)		+= step-chg-jeita.o battery.o pmic-voter.o storm-watch.o
#else
#obj-$(CONFIG_QPNP_SMB2)		+= step-chg-jeita.o battery.o qpnp-smb2.o smb-lib.o pmic-voter.o storm-watch.o
#endif
obj-$(CONFIG_SMB138X_CHARGER)	+= step-chg-jeita.o smb138x-charger.o smb-lib.o pmic-voter.o storm-watch.o battery.o
obj-$(CONFIG_QPNP_QG)		+= qpnp-qg.o pmic-voter.o qg-util.o qg-soc.o qg-sdam.o qg-battery-profile.o qg-profile-lib.o fg-alg.o
obj-$(CONFIG_QPNP_QNOVO)	+= qpnp-qnovo.o battery.o
obj-$(CONFIG_QPNP_TYPEC)	+= qpnp-typec.o
obj-$(CONFIG_QPNP_SMB5)		+= step-chg-jeita.o battery.o qpnp-smb5.o smb5-lib.o pmic-voter.o storm-watch.o schgm-flash.o
obj-$(CONFIG_SMB1390_CHARGE_PUMP)	+= smb1390-charger.o pmic-voter.o
obj-$(CONFIG_QPNP_VM_BMS) += qpnp-vm-bms.o batterydata-lib.o batterydata-interface.o
obj-$(CONFIG_QPNP_LINEAR_CHARGER)	+= qpnp-linear-charger.o
