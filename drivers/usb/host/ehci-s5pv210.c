/* ehci-s5pv210.c - Driver for USB HOST on Samsung S5PV210 processor
 *
 * Bus Glue for SAMSUNG S5PV210 USB HOST EHCI Controller
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * Based on "ehci-au1xxx.c" by by K.Boge <karsten.boge@amd.com>
 * Modified for SAMSUNG s5pv210 EHCI by Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/platform_device.h>
#if defined(CONFIG_PM)
#include <plat/s5pv210.h>
#include <plat/gpio-cfg.h>
#include <linux/delay.h>
#include <linux/gpio.h>
extern unsigned int modem_vid;
extern unsigned int modem_pid;
extern unsigned int device_vid;
extern unsigned int device_pid;
#endif
static struct clk *usb_clk;

extern int usb_disabled(void);

extern void usb_host_phy_init(void);
extern void usb_host_phy_off(void);

static void s5pv210_start_ehc(void);
static void s5pv210_stop_ehc(void);
static int ehci_hcd_s5pv210_drv_probe(struct platform_device *pdev);
static int ehci_hcd_s5pv210_drv_remove(struct platform_device *pdev);

#ifdef CONFIG_PM
static int ehci_hcd_s5pv210_drv_suspend(
		struct platform_device *pdev,
		pm_message_t message)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	unsigned long flags;
	int rc = 0;

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */

	spin_lock_irqsave(&ehci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);

	/* make sure snapshot being resumed re-enumerates everything */
	if (message.event == PM_EVENT_PRETHAW) {
		ehci_halt(ehci);
		ehci_reset(ehci);
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	s5pv210_stop_ehc();
bail:
	spin_unlock_irqrestore(&ehci->lock, flags);

	return rc;
}
static int ehci_hcd_s5pv210_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	s5pv210_start_ehc();

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (ehci_readl(ehci, &ehci->regs->configured_flag) == FLAG_CF) {
		int	mask = INTR_MASK;

		if (!hcd->self.root_hub->do_remote_wakeup)
			mask &= ~STS_PCD;
		ehci_writel(ehci, mask, &ehci->regs->intr_enable);
		ehci_readl(ehci, &ehci->regs->intr_enable);
		return 0;
	}

	ehci_dbg(ehci, "lost power, restarting\n");
	usb_root_hub_lost_power(hcd->self.root_hub);

	(void) ehci_halt(ehci);
	(void) ehci_reset(ehci);

	/* emptying the schedule aborts any urbs */
	spin_lock_irq(&ehci->lock);
	if (ehci->reclaim)
		end_unlink_async(ehci);
	ehci_work(ehci);
	spin_unlock_irq(&ehci->lock);

	ehci_writel(ehci, ehci->command, &ehci->regs->command);
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	ehci_readl(ehci, &ehci->regs->command);	/* unblock posted writes */

	/* here we "know" root ports should always stay powered */
	ehci_port_power(ehci, 1);

	hcd->state = HC_STATE_SUSPENDED;

	return 0;
}

#else
#define ehci_hcd_s5pv210_drv_suspend NULL
#define ehci_hcd_s5pv210_drv_resume NULL
#endif

#ifdef CONFIG_PM
//steven cai for t34
#include <linux/regulator/consumer.h>
static  struct regulator *usb_anlg_regulator, *usb_dig_regulator, *usb_5v_regulator;
static  struct workqueue_struct *my_work_queue;
static  struct work_struct work;
static unsigned int count = 0;
static bool no_usb_device = false;

static void start_usb_host(void)
{
	printk("%s: %s entered\n",__FILE__, __FUNCTION__);
	/* ldo6 regulator on */
	regulator_enable(usb_dig_regulator);

	/* ldo7 regulator on */
	regulator_enable(usb_anlg_regulator);
	
	/* steven cai: if power on device from scrach, then we must start USB hub power; 
	* if no usb device is inserted, then system suspend will power off USB hub, and here system resume need to start it.
	*/
	if (!count || true == no_usb_device)	{
		/* VDD_5V regulator on */
		regulator_enable(usb_5v_regulator);	
		count = 1; 
	}

	/* VDD_USB5V hub power on: GPB5 */
	s3c_gpio_setpull(S5PV210_GPB(5), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S5PV210_GPB(5), S3C_GPIO_OUTPUT); 
	gpio_set_value(S5PV210_GPB(5), 0); 
}


static void stop_usb_host(void)
{
static int cai_count = 0;
	/* 
	* steven cai: If 3G module is inserted, then we should keep power of USB hub and 3G module,
	* otherwise ttyUSB/ttyACM node will disconnect and re-connect during ehci driver suspend/resume phase.
	* If 3G module is not inserted, then we can lost power of USB hub and 3G module gracefully.
	*/	
	if (0==device_vid && 0==device_pid){  //usb device not inserted
		printk("%s: usb device is NOT inserted\n",__FILE__);
		no_usb_device = true;

		/* VDD_USB5V hub power off: GPB5 */
		s3c_gpio_setpull(S5PV210_GPB(5), S3C_GPIO_PULL_DOWN);
		s3c_gpio_cfgpin(S5PV210_GPB(5), S3C_GPIO_INPUT); 

		/* VDD_5V regulator off */
		regulator_disable(usb_5v_regulator);
	}else {
		printk("%s: usb device(VID:0x%4x,PID:0x%4x) is inserted\n", __FILE__,device_vid, device_pid);
		no_usb_device = false;
	}
	printk("%s: cai_count=%d\n", __FILE__,cai_count++);
	/* ldo7 regulator off */
	regulator_disable(usb_anlg_regulator);
	
	/* ldo6 regulator off */
	regulator_disable(usb_dig_regulator);
}
#endif

static void s5pv210_start_ehc(void)
{
#ifdef	CONFIG_PM
	/*
	* steven cai(shijie.cai@samsung.com)
	* T34: start usb host related ldos, and usb external hub and usb device power 
	*/
	start_usb_host();
#endif

	clk_enable(usb_clk);
	usb_host_phy_init();
}

static void s5pv210_stop_ehc(void)
{
	usb_host_phy_off();
	clk_disable(usb_clk);
#ifdef	CONFIG_PM
	/*
	* steven cai(shijie.cai@samsung.com)
	* T34: stop usb host related ldos, and usb external hub and usb device power
	*
	* Note: if use standard form here,
	* stop_usb_host();
	* then the following bug appears. 
	* 
	* BUG: sleeping function called from invalid context at kernel/mutex.c:94
	* BUG: scheduling while atomic: suspend/13/0x00000002
	*
	* so use work queue instead.
	*/
	INIT_WORK(&work, stop_usb_host);
	queue_work(my_work_queue, &work);
#endif

}

static const struct hc_driver ehci_s5pv210_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "s5pv210 EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	.irq			= ehci_irq,
	.flags			= HCD_MEMORY|HCD_USB2,

	.reset			= ehci_init,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	.get_frame_number	= ehci_get_frame,

	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

//added by caishijie for auto-recognizing different modems
static ssize_t
show_device(struct device *ddev,
		struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%04x:%04x\n", modem_vid,modem_pid);//modem_vid,modem_pid is defined in ../core/hub.c
}
static DEVICE_ATTR(modem_vid_pid, S_IRUGO | S_IWUSR, show_device,NULL);


static int ehci_hcd_s5pv210_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd  *hcd = NULL;
	struct ehci_hcd *ehci = NULL;
	int retval = 0;

	//added by caishijie for auto-recognizing different modems
	device_create_file(&(pdev ->dev), &dev_attr_modem_vid_pid);

	if (usb_disabled())
		return -ENODEV;

	usb_host_phy_off();

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		dev_err(&pdev->dev, "resource[1] is not IORESOURCE_IRQ.\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(&ehci_s5pv210_hc_driver, &pdev->dev, "s5pv210");
	if (!hcd) {
		dev_err(&pdev->dev, "usb_create_hcd failed!\n");
		return -ENODEV;
	}
	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_err(&pdev->dev, "request_mem_region failed!\n");
		retval = -EBUSY;
		goto err1;
	}

	usb_clk = clk_get(&pdev->dev, "usb-host");
	if (IS_ERR(usb_clk)) {
		dev_err(&pdev->dev, "cannot get usb-host clock\n");
		retval = -ENODEV;
		goto err2;
	}

	s5pv210_start_ehc();

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		retval = -ENOMEM;
		goto err2;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(readl(&ehci->caps->hc_capbase));
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

#if defined(CONFIG_ARCH_S5PV210) || defined(CONFIG_ARCH_S5P6450)
	writel(0x000F0000, hcd->regs + 0x90);
#endif

#if defined(CONFIG_ARCH_S5PV310)
	writel(0x03C00000, hcd->regs + 0x90);
#endif

	retval = usb_add_hcd(hcd, pdev->resource[1].start,
				IRQF_DISABLED | IRQF_SHARED);

	if (retval == 0) {
		platform_set_drvdata(pdev, hcd);
		return retval;
	}

	s5pv210_stop_ehc();
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	clk_put(usb_clk);
	usb_put_hcd(hcd);
	return retval;
}

static int ehci_hcd_s5pv210_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	s5pv210_stop_ehc();
	clk_put(usb_clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver  ehci_hcd_s5pv210_driver = {
	.probe		= ehci_hcd_s5pv210_drv_probe,
	.remove		= ehci_hcd_s5pv210_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.suspend	= ehci_hcd_s5pv210_drv_suspend,
	.resume		= ehci_hcd_s5pv210_drv_resume,
	.driver = {
		.name = "s5p-ehci",
		.owner = THIS_MODULE,
	}
};

MODULE_ALIAS("platform:s5p-ehci");