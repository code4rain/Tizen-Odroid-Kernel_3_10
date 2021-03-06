/*
 * mms_ts.h - Platform data for Melfas MMS-series touch driver
 *
 * Copyright (C) 2011 Google Inc.
 * Author: Dima Zavin <dima@android.com>
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _LINUX_MMS_TOUCH_H
#define _LINUX_MMS_TOUCH_H
#define MELFAS_TS_NAME "mms128"

struct melfas_tsi_platform_data {
	int	max_x;
	int	max_y;

	int	invert_x;
	int	invert_y;

	int	(*mux_fw_flash)(bool to_gpios);
	int	fw_bin_ver[3];
	const char	*config_fw_version;
	void	(*input_event)(void *data);
	void	(*register_cb)(void *);
};
extern struct class *sec_class;
void tsp_charger_infom(bool en);

#endif /* _LINUX_MMS_TOUCH_H */
