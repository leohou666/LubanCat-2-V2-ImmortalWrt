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
- **网口时序修正**：
  - 对齐官方 125MHz 时钟与复位延时；补齐 XPCS 扩展时钟枚举索引。
  - GMAC0（`fe2a0000`）TX/RX 延时维持官方 `0x18/0x07`。
  - GMAC1（`fe010000`）TX/RX 延时调整为 **`0x00/0x07`**（原官方 `0x22` 在千兆下存在严重丢包与对端 FCS 校验错误）。
  - 两颗 PHY 均保持启用硬件内部 TX/RX 2ns 延迟。

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

- **接口划分**：通过设备树 `label` 显式绑定网口，避免启动顺序漂移：
  - `eth0` (WAN, DHCP 客户端)：绑定 GMAC0 (`fe2a0000`)
  - `eth1` (LAN, 桥接 `br-lan`)：绑定 GMAC1 (`fe010000`)，默认 IP `192.168.3.1`
- **SQM 预设**：开箱即用集成 SQM，首次开机自动应用：
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

## 网络压测步骤

拓扑：`测试 PC (LAN)` ↔ `鲁班猫 2` ↔ `iperf3 服务端 (WAN)`。

### 1. 准备（板端）

测硬件极限转发前，临时关闭默认 SQM 限速：

```bash
/etc/init.d/sqm stop
```

### 2. 吞吐压测（测试 PC 端）

服务端在 WAN 侧执行：`iperf3 -s`

在测试 PC（LAN 侧）执行以下场景压测（将 `<Server_IP>` 替换为 WAN 侧服务器地址）：

#### TCP 吞吐测试（大包打满千兆约 930–940 Mbps）
```bash
# 下载 (WAN -> LAN)
iperf3 -c <Server_IP> -P 4 -t 60 -R --get-server-output

# 上传 (LAN -> WAN)
iperf3 -c <Server_IP> -P 4 -t 60 --get-server-output
```

#### UDP 场景压测
```bash
# 1. 轻载小包业务验证（128B 负载 / 256 Kbps / ~250 PPS）
iperf3 -c <Server_IP> -u -l 128 -b 256K -t 60 -R

# 2. 游戏串流模拟（1408B 负载 / 150 Mbps，参考 Moonlight 大包持续转发）
iperf3 -c <Server_IP> -u -l 1408 -b 150M -t 30 -R

# 3. 极限高 PPS 压测（64B 负载 / 四流各 40M 合计 160M / ~31 万 PPS）
iperf3 -c <Server_IP> -u -l 64 -P 4 -b 40M --pacing-timer 100 -t 30 -R

# 4. 多核分流对照（64B 负载 / 八流各 20M 合计 160M，观察 RPS 核心分布）
iperf3 -c <Server_IP> -u -l 64 -P 8 -b 20M --pacing-timer 100 -t 30 -R
```

### 3. 错误排查与指标说明（板端）

压测前后检查硬件错误、软中断负荷及队列状态：

```bash
# 1. 检查物理层与 MAC 硬件错误计数
for dev in eth0 eth1; do
    echo "=== $dev ==="
    ethtool -S "$dev" | grep -iE 'crc|error|drop|miss|overflow'
done

# 2. 查看多核软中断状态（按 CPU0~CPU3 逐行排列，十六进制）
cat /proc/net/softnet_stat

# 3. 查看出向队列丢包
tc -s qdisc show
```

**关键指标含义：**

| 指标                                              | 出现增量代表的含义                                                                 |
| ------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `rx_crc_errors` / `mmc_rx_crc_error`          | **时序或硬件链路异常**：RGMII 延时不对齐、线缆不良。正常压测下必须为 `0`。 |
| `mmc_rx_fifo_overflow`                          | **MAC 硬件接收 FIFO 溢出**：超高 PPS 极限压测时，DMA 搬运速度不及收包速率。  |
| `softnet_stat` **第 2 列** (backlog drop) | **CPU 软中断队列溢出**：该核心协议栈处理饱和，主动丢包。                     |
| `softnet_stat` **第 3 列** (time squeeze) | **CPU 处于高负载**：软中断单次轮询耗尽了处理配额（`netdev_budget`）。      |
| `tc qdisc` 中的 `dropped`                     | **出口流控丢包**：出向带宽饱和或队列满导致的正常排队丢包。                   |

## 实测表现

- **TCP 下载转发**：GMAC1 TX=`0x00`、关闭 SQM，四流 60 秒合计 **929 Mbps**（传输 6.49 GB），TCP 0 重传，PC 端 RX FCS 校验错误无增长。
- **SQM 流控表现**：开启 750 Mbps（`TBF` + `fq_codel`）时，实测吞吐约 **585 Mbps**。
- **小包压测**：64 字节 UDP 多流吞吐约 **160～171 Mbps**（折合 **31～33 万 PPS**）；更高目标码率下的实际发包能力仍需确认。
- **多核调优结论**：实测 WAN RPS 掩码设为 `5`（分流 CPU0/2）综合表现最优，设为全核心 `f` 时存在性能衰退。

> **供电提示**：如果Type-C直连电脑遇到反复重启，可能是多核满载及大流量压测时峰值功耗较高，建议使用 5V/3A 及以上电源，避免供电抖动。

## 进阶：GMAC1 链路在线诊断与调谐

固件内置了 RGMII 在线诊断模块（`CONFIG_DWMAC_ROCKCHIP_LUBANCAT2_DIAGNOSTICS=y`），支持在串口终端实时查看 PHY 延迟状态并调试 GMAC1 的 TX 延时档位：

### 1. 查看状态

```bash
# 查看 GMAC1 MAC 延时及状态
cat /sys/class/net/eth1/device/rgmii_diag/status

# 查看两颗 PHY 的内部 2ns 延时寄存器状态
cat /sys/class/net/eth0/phydev/rgmii_diag/phy_status
cat /sys/class/net/eth1/phydev/rgmii_diag/phy_status
```

### 2. 在线微调 TX 延时（临时生效）

```bash
# 实时写入测试档位（范围 0x00 ~ 0x7f）
echo 0x04 > /sys/class/net/eth1/device/rgmii_diag/tx_delay

# 恢复设备树默认值 (0x00)
echo restore > /sys/class/net/eth1/device/rgmii_diag/tx_delay
```

*注：该写入仅在内存中临时生效，重启或控制器重新初始化后自动恢复设备树默认值。*

## 关键代码索引

| 模块                   | 对应路径                                                                                                                                                        |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 固件板型定义           | [target/linux/rockchip/image/armv8.mk](target/linux/rockchip/image/armv8.mk)                                                                                     |
| U-Boot 构建配置        | [package/boot/uboot-rockchip/Makefile](package/boot/uboot-rockchip/Makefile)                                                                                     |
| JLSemi PHY 驱动及诊断  | [target/linux/rockchip/files/drivers/net/phy/](target/linux/rockchip/files/drivers/net/phy/)                                                                     |
| 网口名称/时序/驱动补丁 | [target/linux/rockchip/patches-6.12/](target/linux/rockchip/patches-6.12/)                                                                                       |
| IRQ / RPS 亲和性脚本   | [target/linux/rockchip/armv8/base-files/usr/libexec/platform/packet-steering.sh](target/linux/rockchip/armv8/base-files/usr/libexec/platform/packet-steering.sh) |
| SQM 首次配置预设       | [target/linux/rockchip/armv8/base-files/etc/uci-defaults/99-lubancat2-sqm](target/linux/rockchip/armv8/base-files/etc/uci-defaults/99-lubancat2-sqm)             |

## 致谢与参考

- [ImmortalWrt](https://github.com/immortalwrt/immortalwrt) / [OpenWrt](https://openwrt.org/)
- [DHDAXCW/lede-rockchip](https://github.com/DHDAXCW/lede-rockchip)：JLSemi 驱动与 Rockchip 适配参考
- [野火 EmbedFire](https://doc.embedfire.com/)：LubanCat 2 官方板级资料与设备树
