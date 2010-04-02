/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/spi/cpcap.h>

#define SECS_PER_DAY 86400
#define DAY_MASK  0x7FFF
#define TOD1_MASK 0x00FF
#define TOD2_MASK 0x01FF

struct cpcap_time {
	unsigned short day;
	unsigned short tod1;
	unsigned short tod2;
};

struct cpcap_rtc {
	struct cpcap_device *cpcap;
	struct rtc_device *rtc_dev;
	int alarm_enabled;
	int second_enabled;
};

static void cpcap2rtc_time(struct rtc_time *rtc, struct cpcap_time *cpcap)
{
	unsigned long int tod;
	unsigned long int time;

	tod = (cpcap->tod1 & TOD1_MASK) | ((cpcap->tod2 & TOD2_MASK) << 8);
	time = tod + ((cpcap->day & DAY_MASK) * SECS_PER_DAY);

	rtc_time_to_tm(time, rtc);
}

static void rtc2cpcap_time(struct cpcap_time *cpcap, struct rtc_time *rtc)
{
	unsigned long time;

	rtc_tm_to_time(rtc, &time);

	cpcap->day = time / SECS_PER_DAY;
	time %= SECS_PER_DAY;
	cpcap->tod2 = (time >> 8) & TOD2_MASK;
	cpcap->tod1 = time & TOD1_MASK;
}

static int
cpcap_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct cpcap_rtc *rtc = dev_get_drvdata(dev);
	int err;

	if (enabled)
		err = cpcap_irq_unmask(rtc->cpcap, CPCAP_IRQ_TODA);
	else
		err = cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_TODA);

	if (err < 0)
		return err;

	rtc->alarm_enabled = enabled;

	return 0;
}

static int
cpcap_rtc_update_irq_enable(struct device *dev, unsigned int enabled)
{
	struct cpcap_rtc *rtc = dev_get_drvdata(dev);
	int err;

	if (enabled)
		err = cpcap_irq_unmask(rtc->cpcap, CPCAP_IRQ_1HZ);
	else
		err = cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_1HZ);

	if (err < 0)
		return err;

	rtc->second_enabled = enabled;

	return 0;
}

static int cpcap_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	unsigned short temp_tod2;
	int ret;

	rtc = dev_get_drvdata(dev);

	ret = cpcap_regacc_read(rtc->cpcap, CPCAP_REG_TOD2, &temp_tod2);
	ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_DAY, &cpcap_tm.day);
	ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_TOD1, &cpcap_tm.tod1);
	ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_TOD2, &cpcap_tm.tod2);
	if (temp_tod2 > cpcap_tm.tod2)
		ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_DAY,
					 &cpcap_tm.day);

	if (ret) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	cpcap2rtc_time(tm, &cpcap_tm);

	dev_dbg(dev, "RTC_TIME: %u.%u.%u %u:%u:%u\n",
		tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return rtc_valid_tm(tm);
}

static int cpcap_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int second_masked;
	int alarm_masked;
	int ret;

	rtc = dev_get_drvdata(dev);

	dev_dbg(dev, "RTC_TIME: %u.%u.%u %u:%u:%u\n",
		tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc2cpcap_time(&cpcap_tm, tm);

	second_masked = cpcap_irq_mask_get(rtc->cpcap, CPCAP_IRQ_1HZ);
	alarm_masked = cpcap_irq_mask_get(rtc->cpcap, CPCAP_IRQ_TODA);

	if (!second_masked)
		cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_1HZ);
	if (!alarm_masked)
		cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_TODA);

	/* Clearing the upper lower 8 bits of the TOD guarantees that the
	 * upper half of TOD (TOD2) will not increment for 0xFF RTC ticks
	 * (255 seconds).  During this time we can safely write to DAY, TOD2,
	 * then TOD1 (in that order) and expect RTC to be synchronized to
	 * the exact time requested upon the final write to TOD1. */
	ret = cpcap_regacc_write(rtc->cpcap, CPCAP_REG_TOD1, 0, TOD1_MASK);
	ret |= cpcap_regacc_write(rtc->cpcap, CPCAP_REG_DAY, cpcap_tm.day,
				  DAY_MASK);
	ret |= cpcap_regacc_write(rtc->cpcap, CPCAP_REG_TOD2, cpcap_tm.tod2,
				  TOD2_MASK);
	ret |= cpcap_regacc_write(rtc->cpcap, CPCAP_REG_TOD1, cpcap_tm.tod1,
				  TOD1_MASK);

	if (!second_masked)
		cpcap_irq_unmask(rtc->cpcap, CPCAP_IRQ_1HZ);
	if (!alarm_masked)
		cpcap_irq_unmask(rtc->cpcap, CPCAP_IRQ_TODA);

	return ret;
}

static int cpcap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int ret;

	rtc = dev_get_drvdata(dev);

	alrm->enabled = rtc->alarm_enabled;

	ret = cpcap_regacc_read(rtc->cpcap, CPCAP_REG_DAYA, &cpcap_tm.day);
	ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_TODA2, &cpcap_tm.tod2);
	ret |= cpcap_regacc_read(rtc->cpcap, CPCAP_REG_TODA1, &cpcap_tm.tod1);

	if (ret) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	cpcap2rtc_time(&alrm->time, &cpcap_tm);

	return rtc_valid_tm(&alrm->time);
}

static int cpcap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int alarm_masked;
	int ret;

	rtc = dev_get_drvdata(dev);

	rtc2cpcap_time(&cpcap_tm, &alrm->time);

	alarm_masked = cpcap_irq_mask_get(rtc->cpcap, CPCAP_IRQ_TODA);

	if (!alarm_masked)
		cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_TODA);

	ret = cpcap_regacc_write(rtc->cpcap, CPCAP_REG_DAYA, cpcap_tm.day,
				 DAY_MASK);
	ret |= cpcap_regacc_write(rtc->cpcap, CPCAP_REG_TODA2, cpcap_tm.tod2,
				  TOD2_MASK);
	ret |= cpcap_regacc_write(rtc->cpcap, CPCAP_REG_TODA1, cpcap_tm.tod1,
				  TOD1_MASK);

	if (!alarm_masked)
		cpcap_irq_unmask(rtc->cpcap, CPCAP_IRQ_TODA);

	return ret;
}

static struct rtc_class_ops cpcap_rtc_ops = {
	.read_time		= cpcap_rtc_read_time,
	.set_time		= cpcap_rtc_set_time,
	.read_alarm		= cpcap_rtc_read_alarm,
	.set_alarm		= cpcap_rtc_set_alarm,
	.alarm_irq_enable 	= cpcap_rtc_alarm_irq_enable,
	.update_irq_enable 	= cpcap_rtc_update_irq_enable,
};

static void cpcap_rtc_irq(enum cpcap_irqs irq, void *data)
{
	struct cpcap_rtc *rtc = data;

	switch (irq) {
	case CPCAP_IRQ_TODA:
		rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);
		break;
	case CPCAP_IRQ_1HZ:
		rtc_update_irq(rtc->rtc_dev, 1, RTC_UF | RTC_IRQF);
		break;
	default:
		break;
	}
}

static int __devinit cpcap_rtc_probe(struct platform_device *pdev)
{
	struct cpcap_rtc *rtc;

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->cpcap = pdev->dev.platform_data;
	platform_set_drvdata(pdev, rtc);
	rtc->rtc_dev = rtc_device_register("cpcap_rtc", &pdev->dev,
					   &cpcap_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc->rtc_dev)) {
		kfree(rtc);
		return PTR_ERR(rtc->rtc_dev);
	}

	cpcap_irq_register(rtc->cpcap, CPCAP_IRQ_TODA, cpcap_rtc_irq, rtc);
	cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_TODA);

	cpcap_irq_clear(rtc->cpcap, CPCAP_IRQ_1HZ);
	cpcap_irq_register(rtc->cpcap, CPCAP_IRQ_1HZ, cpcap_rtc_irq, rtc);
	cpcap_irq_mask(rtc->cpcap, CPCAP_IRQ_1HZ);

	return 0;
}

static int __devexit cpcap_rtc_remove(struct platform_device *pdev)
{
	struct cpcap_rtc *rtc;

	rtc = platform_get_drvdata(pdev);

	cpcap_irq_free(rtc->cpcap, CPCAP_IRQ_TODA);
	cpcap_irq_free(rtc->cpcap, CPCAP_IRQ_1HZ);

	rtc_device_unregister(rtc->rtc_dev);
	kfree(rtc);

	return 0;
}

static struct platform_driver cpcap_rtc_driver = {
	.driver = {
		.name = "cpcap_rtc",
	},
	.probe = cpcap_rtc_probe,
	.remove = __devexit_p(cpcap_rtc_remove),
};

static int __init cpcap_rtc_init(void)
{
	return platform_driver_register(&cpcap_rtc_driver);
}
module_init(cpcap_rtc_init);

static void __exit cpcap_rtc_exit(void)
{
	platform_driver_unregister(&cpcap_rtc_driver);
}
module_exit(cpcap_rtc_exit);

MODULE_ALIAS("platform:cpcap_rtc");
MODULE_DESCRIPTION("CPCAP RTC driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
