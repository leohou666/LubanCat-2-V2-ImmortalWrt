/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _JLSEMI_LUBANCAT_DIAG_H
#define _JLSEMI_LUBANCAT_DIAG_H

#include <linux/sysfs.h>

#if IS_ENABLED(CONFIG_DWMAC_ROCKCHIP_LUBANCAT2_DIAGNOSTICS)
static ssize_t phy_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct phy_device *phydev = to_phy_device(dev);
	int oldpage, value, ret, restore_ret;

	if (!mutex_trylock(&phydev->lock))
		return -EBUSY;
	if (phydev->suspended) {
		ret = -EBUSY;
		goto out;
	}

	/* Serialize page selection, read and restore against all MDIO users. */
	phy_lock_mdio_bus(phydev);
	oldpage = __phy_read(phydev, JLSEMI_PAGE31);
	if (oldpage < 0) {
		ret = oldpage;
		goto unlock_bus;
	}
	ret = __phy_write(phydev, JLSEMI_PAGE31, JL2XXX_PAGE3336);
	if (ret < 0)
		goto restore_page;
	value = __phy_read(phydev, JL2XXX_RGMII_CTRL_REG);
	ret = value < 0 ? value : 0;
restore_page:
	restore_ret = __phy_write(phydev, JLSEMI_PAGE31, oldpage);
	if (restore_ret < 0)
		ret = restore_ret;
unlock_bus:
	phy_unlock_mdio_bus(phydev);
	if (!ret)
		ret = sysfs_emit(buf,
			"page=0x0d08\nregister=0x11\nvalue=0x%04x\n"
			"tx_delay_2ns=%u\nrx_delay_2ns=%u\n",
			value, !!(value & JL2XXX_RGMII_TX_DLY_2NS),
			!!(value & JL2XXX_RGMII_RX_DLY_2NS));
out:
	mutex_unlock(&phydev->lock);
	return ret;
}
static DEVICE_ATTR_RO(phy_status);

static struct attribute *jlsemi_lubancat_diag_attrs[] = {
	&dev_attr_phy_status.attr,
	NULL,
};

static const struct attribute_group jlsemi_lubancat_diag_group = {
	.name = "rgmii_diag",
	.attrs = jlsemi_lubancat_diag_attrs,
};

static int jlsemi_lubancat_diag_add(struct phy_device *phydev)
{
	if (!of_machine_is_compatible("embedfire,lubancat-2"))
		return 0;
	return devm_device_add_group(&phydev->mdio.dev,
				    &jlsemi_lubancat_diag_group);
}
#else
static inline int jlsemi_lubancat_diag_add(struct phy_device *phydev)
{
	return 0;
}
#endif
#endif
