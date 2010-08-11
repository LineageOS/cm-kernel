/*
 * Copyright (C) 2008 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <mach/msm_fb.h>

static DECLARE_WAIT_QUEUE_HEAD(novtec_vsync_wait);

struct panel_info {
	struct msm_mddi_client_data *client_data;
	struct platform_device pdev;
	struct msm_panel_data panel_data;
	struct msmfb_callback *novtec_callback;
	struct wake_lock idle_lock;
	int novtec_got_int;
	int irq;
};

static struct platform_device mddi_nov_cabc = {
	.name = "nov_cabc",
	.id = 0,
};

static void novtec_request_vsync(struct msm_panel_data *panel_data,
				  struct msmfb_callback *callback)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	panel->novtec_callback = callback;
	if (panel->novtec_got_int) {
		panel->novtec_got_int = 0;
		client_data->activate_link(client_data);
	}
}

static void novtec_clear_vsync(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	client_data->activate_link(client_data);
}

static void novtec_wait_vsync(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	if (panel->novtec_got_int) {
		panel->novtec_got_int = 0;
		client_data->activate_link(client_data); /* clears interrupt */
	}
	if (wait_event_timeout(novtec_vsync_wait, panel->novtec_got_int,
				HZ/2) == 0)
		printk(KERN_ERR "timeout waiting for VSYNC\n");
	panel->novtec_got_int = 0;
	/* interrupt clears when screen dma starts */
}

static int novtec_suspend(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	int ret;

	wake_lock(&panel->idle_lock);
	ret = bridge_data->uninit(bridge_data, client_data);
	wake_unlock(&panel->idle_lock);
	if (ret) {
		printk(KERN_INFO "mddi novtec client: non zero return from "
			"uninit\n");
		return ret;
	}
	client_data->suspend(client_data);
	return 0;
}

static int novtec_resume(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	int ret;

	wake_lock(&panel->idle_lock);
	client_data->resume(client_data);
	wake_unlock(&panel->idle_lock);
	ret = bridge_data->init(bridge_data, client_data);
	if (ret)
		return ret;
	return 0;
}

static int novtec_blank(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;

	return bridge_data->blank(bridge_data, client_data);
}

static int novtec_unblank(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;

	return bridge_data->unblank(bridge_data, client_data);
}

static irqreturn_t novtec_vsync_interrupt(int irq, void *data)
{
	struct panel_info *panel = data;

	panel->novtec_got_int = 1;
	if (panel->novtec_callback) {
//		mdelay(3);
		panel->novtec_callback->func(panel->novtec_callback);
		panel->novtec_callback = 0;
	}
	wake_up(&novtec_vsync_wait);
	return IRQ_HANDLED;
}

static int mddi_novtec_probe(struct platform_device *pdev)
{
	int ret;
	struct msm_mddi_client_data *client_data = pdev->dev.platform_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	struct panel_info *panel =
		kzalloc(sizeof(struct panel_info), GFP_KERNEL);

	if (!panel)
		return -ENOMEM;
	platform_set_drvdata(pdev, panel);

	if (bridge_data->panel_caps & MSMFB_CAP_CABC) {
		printk(KERN_INFO "CABC enabled\n");
		mddi_nov_cabc.dev.platform_data = client_data;
		platform_device_register(&mddi_nov_cabc);
	}

	ret = platform_get_irq_byname(pdev, "vsync");
	if (ret < 0)
		goto err_plat_get_irq;

	panel->irq = ret;
	printk(KERN_DEBUG "%s: Requesting irq=%d; value=%d\n", __func__, ret, gpio_get_value(98));
	ret = request_irq(panel->irq, novtec_vsync_interrupt,
			  IRQF_TRIGGER_FALLING, "vsync", panel);
	if (ret) {
		dev_err(&pdev->dev, "mddi_bridge_setup_vsync failed\n");
		goto err_req_irq;
	}

	panel->client_data = client_data;
	panel->panel_data.suspend = novtec_suspend;
	panel->panel_data.resume = novtec_resume;
	panel->panel_data.wait_vsync = novtec_wait_vsync;
	panel->panel_data.request_vsync = novtec_request_vsync;
	panel->panel_data.clear_vsync = novtec_clear_vsync;
	panel->panel_data.blank = novtec_blank;
	panel->panel_data.unblank = novtec_unblank;
	panel->panel_data.fb_data =  &bridge_data->fb_data;
	panel->panel_data.caps = MSMFB_CAP_PARTIAL_UPDATES;

	panel->pdev.name = "msm_panel";
	panel->pdev.id = pdev->id;
	panel->pdev.resource = client_data->fb_resource;
	panel->pdev.num_resources = 1;
	panel->pdev.dev.platform_data = &panel->panel_data;
	platform_device_register(&panel->pdev);
	wake_lock_init(&panel->idle_lock, WAKE_LOCK_IDLE, "nov_idle_lock");

	return 0;

err_req_irq:
err_plat_get_irq:
	kfree(panel);
	return ret;
}

static int mddi_novtec_remove(struct platform_device *pdev)
{
	struct panel_info *panel = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	free_irq(panel->irq, panel);
	kfree(panel);
	return 0;
}

static struct platform_driver mddi_client_d263_0000 = {
	.probe = mddi_novtec_probe,
	.remove = mddi_novtec_remove,
	.driver = { .name = "mddi_c_b9f6_5582" },
};

static int __init mddi_client_novtec_init(void)
{
	platform_driver_register(&mddi_client_d263_0000);
	return 0;
}

module_init(mddi_client_novtec_init);

