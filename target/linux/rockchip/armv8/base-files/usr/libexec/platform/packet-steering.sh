#!/bin/sh

. /lib/functions.sh

# Keep netifd's queue/flow configuration and the default policy on other boards.
flows="$(uci -q get 'network.@globals[0].steering_flows')"
if [ "${flows:-0}" -gt 0 ]; then
	/usr/libexec/network/packet-steering.uc -l "$flows" "$1"
else
	/usr/libexec/network/packet-steering.uc "$1"
fi

[ "$(board_name)" = "embedfire,lubancat-2" ] || exit 0

cpu_online() {
	[ "$1" = 0 ] || [ "$(cat "/sys/devices/system/cpu/cpu$1/online" 2>/dev/null)" = 1 ]
}

set_interface_irq_cpu() {
	local interface="$1" cpu="$2" irq

	cpu_online "$cpu" || cpu=0
	for irq in $(awk -v dev="$interface" '$NF ~ ("^" dev "($|[-:])") {
		sub(/:$/, "", $1); print $1
	}' /proc/interrupts); do
		[ -w "/proc/irq/$irq/smp_affinity_list" ] || continue
		echo "$cpu" > "/proc/irq/$irq/smp_affinity_list"
	done
}

# Separate WAN and LAN IRQ/NAPI work from RPS protocol processing.
set_interface_irq_cpu eth0 1
set_interface_irq_cpu eth1 3

# Honour LuCI's disabled/all-CPU steering modes; use our tested mask otherwise.
case "$1" in
	0|2) exit 0 ;;
esac

mask=1
cpu_online 2 && mask=5
for queue in /sys/class/net/eth0/queues/rx-*/rps_cpus \
	/sys/class/net/eth1/queues/rx-*/rps_cpus; do
	[ -w "$queue" ] || continue
	echo "$mask" > "$queue"
done

exit 0
