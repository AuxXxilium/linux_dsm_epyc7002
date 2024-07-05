#ifndef __SOC_REALTEK_PD_H
#define __SOC_REALTEK_PD_H

enum rtk_pd_notication {
	RTK_PD_NOTIFY_PRE_OFF = 0,
	RTK_PD_NOTIFY_OFF,
	RTK_PD_NOTIFY_PRE_ON,
	RTK_PD_NOTIFY_ON
};

int rtk_pd_dev_pm_add_notifier(struct device *dev, struct notifier_block *nb);
void rtk_pd_dev_pm_remove_notifier(struct device *dev);

#endif
