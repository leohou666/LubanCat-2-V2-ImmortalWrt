# ImmortalWrt for LubanCat 2 V2

基于 [ImmortalWrt](https://github.com/immortalwrt/immortalwrt) (`openwrt-25.12` / Linux 6.12) 为野火 **LubanCat 2 V2** 适配的固件。

集成了 JLSemi PHY 网卡驱动、官方时序补丁、PWM 自动温控风扇，并针对多核做了网卡中断绑核与开箱即用的 SQM 流控预设。

## 硬件规格与支持

- **硬件型号**：野火 LubanCat 2 V2（RK3568，4GB RAM，双千兆网口，JLSemi PHY）
- **内核与引导**：Linux 6.12 + U-Boot 2025.10
- **构建目标**：`Rockchip` -> `ARMv8` -> `EmbedFire LubanCat 2` (`embedfire_lubancat-2`)
- **引导介质**：TF 卡 / eMMC

## 核心适配与优化

### 1. JLSemi 网卡驱动与时序对齐
- **驱动移植**：移植 JLSemi 1.2.9 PHY 驱动并适配 Linux 6.12 字符串 API（正确识别 PHY ID `0x937c4032` 为 `JL2xxx Gigabit Ethernet`），解决通用驱动下的 DMA 初始化超时与反复协商断流问题。
- **时序对齐**：按照官方设备树对齐 GMAC0（`0x18/0x07`）与 GMAC1（`0x22/0x07`）TX/RX 延迟、125MHz 时钟配置及复位延时。

### 2. 中断与 RPS 多核分配
通过 netifd 的 platform packet-steering 脚本自动绑定核心，避免流量压在单核：
- `eth0` 中断：绑定 **CPU 1**
- `eth1` 中断：绑定 **CPU 3**
- 接收端 RPS：分流至 **CPU 0、CPU 2**（掩码 `0x5`）

### 3. PWM 自动温控风扇
使用 `pwm-fan` 驱动（PWM0_M0，20kHz），阶梯温控，兼顾静音与散热：
- `< 40°C`：停转
- `40°C`：档位 100
- `45°C`：档位 150
- `50°C`：档位 200
- `≥ 55°C`：全速 255
- 各档位预设 2°C 回差，避免临界温度频繁切档。

### 4. 预设网络与 SQM 流控
- **接口划分**：`eth0` 为 WAN（DHCP 客户端），`eth1` 为 LAN（桥接 `br-lan`）。
- **默认网段**：建议配置为 `192.168.3.1`，避免与常见上级光猫冲突。
- **SQM 预设**：集成 SQM，首次开机自动应用：
  - 绑定 `eth0` (WAN)
  - 下载限速 750 Mbps，上传不限
  - 队列算法：`fq_codel` + `simplest_tbf.qos`

## 快速编译

### 1. 环境与配置
推荐使用 Debian 12 / 13 构建环境（Python ≥ 3.10）：

```bash
./scripts/feeds update -a
./scripts/feeds install -a
make menuconfig
```

### 2. 目标选择
在 menuconfig 中配置：
```text
Target System  -> Rockchip
Subtarget      -> ARMv8
Target Profile -> EmbedFire LubanCat 2
```

推荐可选包（按需勾选 `<*>`）：
- 排障工具：`ethtool-full`、`tcpdump`、`htop`
- 策略路由：`luci-app-pbr`（注意勾选 `ip-full`，取消 `ip-tiny` 避免包冲突）
- VPN 组件：`luci-proto-wireguard`、`wireguard-tools`

### 3. 编译
```bash
make defconfig
make download -j8
make -j$(nproc)
```
编译产物位于 `bin/targets/rockchip/armv8/`，生成 `ext4-sysupgrade.img.gz` 和 `squashfs-sysupgrade.img.gz` 完整整盘镜像，可直接写盘。

## 实测表现

- **转发吞吐**：双向 TCP 转发约 830 Mbps。
- **小包压测**：64 字节 UDP 四流合计 160 Mbps（约 31.25 万 PPS），长时间压测零丢包。
- **网络稳定性**：JLSemi PHY 驱动工作稳定，网口热拔插恢复正常。

> **供电提示**：多核满载及大流量压测时峰值功耗较高，建议使用 5V/3A 及以上电源，避免供电抖动导致重启。

## 关键代码索引

| 模块 | 对应路径 |
|---|---|
| 固件板型定义 | [target/linux/rockchip/image/armv8.mk](target/linux/rockchip/image/armv8.mk) |
| U-Boot 构建配置 | [package/boot/uboot-rockchip/Makefile](package/boot/uboot-rockchip/Makefile) |
| JLSemi PHY 驱动 | [target/linux/rockchip/files/drivers/net/phy/](target/linux/rockchip/files/drivers/net/phy/) |
| 6.12 内核/设备树补丁 | [target/linux/rockchip/patches-6.12/](target/linux/rockchip/patches-6.12/) |
| IRQ / RPS 亲和性脚本 | [target/linux/rockchip/armv8/base-files/usr/libexec/platform/packet-steering.sh](target/linux/rockchip/armv8/base-files/usr/libexec/platform/packet-steering.sh) |
| SQM 首次配置预设 | [target/linux/rockchip/armv8/base-files/etc/uci-defaults/99-lubancat2-sqm](target/linux/rockchip/armv8/base-files/etc/uci-defaults/99-lubancat2-sqm) |

## 致谢与参考

- [ImmortalWrt](https://github.com/immortalwrt/immortalwrt) / [OpenWrt](https://openwrt.org/)
- [DHDAXCW/lede-rockchip](https://github.com/DHDAXCW/lede-rockchip)：JLSemi 驱动与 Rockchip 适配参考
- [野火 EmbedFire](https://doc.embedfire.com/)：LubanCat 2 官方板级资料与设备树
