/* Copyright (C) 2007 Google Incorporated
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

#ifndef _MSM_NOTIFY_H_
#define _MSM_NOTIFY_H_

/*
 * This is used to communicate event between msm_fb, mddi, mddi_client, 
 * and board.
 * It's mainly used to reset the display system.
 * Also, it is used for battery power policy.
 *
 */
#define NOTIFY_MDDI     0x00000000
#define NOTIFY_POWER    0x00000001
#define NOTIFY_MSM_FB   0x00000010

#if 0
extern int register_display_notifier(struct notifier_block *nb);
extern int display_notifier_call_chain(unsigned long val, void *data);
 
#define display_notifier(fn, pri) {                     \
	static struct notifier_block fn##_nb =          \
	{ .notifier_call = fn, .priority = pri };       \
	register_display_notifier(&fn##_nb);		\
}
#endif

#endif
