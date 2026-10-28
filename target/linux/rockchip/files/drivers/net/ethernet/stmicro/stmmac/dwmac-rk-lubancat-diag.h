/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _DWMAC_RK_LUBANCAT_DIAG_H
#define _DWMAC_RK_LUBANCAT_DIAG_H

#include <linux/rtnetlink.h>
#include <linux/sysfs.h>

#if IS_ENABLED(CONFIG_DWMAC_ROCKCHIP_LUBANCAT2_DIAGNOSTICS)
static bool rk_lubancat_diag_supported(struct device *dev)
{
	struct rk_priv_data *bsp_priv = get_stmmac_bsp_priv(dev);

	return of_machine_is_compatible("embedfire,lubancat-2") &&
	       of_device_is_compatible(dev->of_node, "rockchip,rk3568-gmac") &&
	       bsp_priv->id == 1 &&
	       bsp_priv->phy_iface == PHY_INTERFACE_MODE_RGMII;
}

/* Keep diagnostics out of interface open/close and ethtool configuration. */
static int rk_lubancat_diag_lock(struct device *dev)
{
	struct rk_priv_data *bsp_priv;
	struct net_device *ndev;

	if (!rtnl_trylock())
		return -EBUSY;

	ndev = dev_get_drvdata(dev);
	bsp_priv = get_stmmac_bsp_priv(dev);
	if (!netif_running(ndev) || !bsp_priv->clk_enabled ||
	    bsp_priv->suspended) {
		rtnl_unlock();
		return -ENETDOWN;
	}

	return 0;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rk_priv_data *bsp_priv = get_stmmac_bsp_priv(dev);
	u32 con0, con1;
	int ret;

	ret = rk_lubancat_diag_lock(dev);
	if (ret)
		return ret;
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON0, &con0);
	if (ret)
		goto out;
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON1, &con1);
	if (ret)
		goto out;

	ret = sysfs_emit(buf,
		"controller=fe010000\ncon0=0x%08x\ncon1=0x%08x\n"
		"tx_delay=0x%02x\nrx_delay=0x%02x\n"
		"dt_tx_delay=0x%02x\ndt_rx_delay=0x%02x\n"
		"tx_delay_enabled=%u\nrx_delay_enabled=%u\n"
		"speed_clock_hz=%lu\n",
		con0, con1, con0 & 0x7f, (con0 >> 8) & 0x7f,
		bsp_priv->tx_delay, bsp_priv->rx_delay,
		!!(con1 & BIT(0)), !!(con1 & BIT(1)),
		clk_get_rate(bsp_priv->clks[RK_CLK_MAC_SPEED].clk));
out:
	rtnl_unlock();
	return ret;
}
static DEVICE_ATTR_RO(status);

static ssize_t tx_delay_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rk_priv_data *bsp_priv = get_stmmac_bsp_priv(dev);
	u32 value;
	int ret;

	ret = rk_lubancat_diag_lock(dev);
	if (ret)
		return ret;
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON0, &value);
	if (!ret)
		ret = sysfs_emit(buf, "0x%02x\n", value & 0x7f);
	rtnl_unlock();
	return ret;
}

static ssize_t tx_delay_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rk_priv_data *bsp_priv = get_stmmac_bsp_priv(dev);
	u32 before, after, con1;
	unsigned int value;
	int ret, restore_ret;

	if (sysfs_streq(buf, "restore")) {
		value = bsp_priv->tx_delay;
	} else {
		ret = kstrtouint(buf, 0, &value);
		if (ret)
			return ret;
	}
	if (value > 0x7f)
		return -ERANGE;

	ret = rk_lubancat_diag_lock(dev);
	if (ret)
		return ret;
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON1, &con1);
	if (ret)
		goto out;
	/* Only adjust an already-enabled TX delay in RGMII mode. */
	if ((con1 & GENMASK(6, 4)) != BIT(4) || !(con1 & BIT(0))) {
		ret = -EINVAL;
		goto out;
	}
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON0, &before);
	if (ret)
		goto out;

	/* GRF uses upper-half write enables: touch TX[6:0] only. */
	ret = regmap_write(bsp_priv->grf, RK3568_GRF_GMAC1_CON0,
			   RK3568_GMAC_CLK_TX_DL_CFG(value));
	if (ret)
		goto restore;
	ret = regmap_read(bsp_priv->grf, RK3568_GRF_GMAC1_CON0, &after);
	if (!ret && (after & 0xffff) != ((before & 0xff80) | value))
		ret = -EIO;
	if (ret)
		goto restore;

	/* Leave the DT defaults intact for explicit restore and power-up. */
	dev_info(dev, "diagnostic TX delay 0x%02x -> 0x%02x (RX 0x%02x)\n",
		 before & 0x7f, value, (after >> 8) & 0x7f);
	ret = count;
	goto out;
restore:
	restore_ret = regmap_write(bsp_priv->grf, RK3568_GRF_GMAC1_CON0,
				   RK3568_GMAC_CLK_TX_DL_CFG(before & 0x7f));
	if (restore_ret)
		dev_err(dev, "failed to restore TX delay: %d\n", restore_ret);
out:
	rtnl_unlock();
	return ret;
}
static DEVICE_ATTR_RW(tx_delay);

static struct attribute *rk_lubancat_diag_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_tx_delay.attr,
	NULL,
};

static const struct attribute_group rk_lubancat_diag_group = {
	.name = "rgmii_diag",
	.attrs = rk_lubancat_diag_attrs,
};

static int rk_lubancat_diag_add(struct device *dev)
{
	if (!rk_lubancat_diag_supported(dev))
		return 0;
	return sysfs_create_group(&dev->kobj, &rk_lubancat_diag_group);
}

static void rk_lubancat_diag_remove(struct device *dev)
{
	if (rk_lubancat_diag_supported(dev))
		sysfs_remove_group(&dev->kobj, &rk_lubancat_diag_group);
}
#else
static inline int rk_lubancat_diag_add(struct device *dev)
{
	return 0;
}

static inline void rk_lubancat_diag_remove(struct device *dev)
{
}
#endif
#endif
