/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DEVICE_RELEASE_TEST_H
#define DEVICE_RELEASE_TEST_H

#include <linux/device.h>

int device_release_test_hold(struct device *dev);
void device_release_test_gate(void);

#endif
