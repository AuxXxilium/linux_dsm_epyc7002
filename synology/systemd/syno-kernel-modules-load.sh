#!/bin/sh

check_feature() # $1: key
{
	[ "$(get_key_value "/etc.defaults/synoinfo.conf" "${1:-}")" = "yes" ]
}

probe_modules() # $@: module list
{
	for _m in "$@"; do
		modprobe "$_m"
	done
}

# load synobios
modprobe synobios system_mode=1
/bin/mknod /dev/synobios c 201 0 2>/dev/null

# load usbhid for bromolow
if check_feature "support_dual_head"; then
	probe_modules usblp hid usbhid hid-generic
fi

# load syno_hddmon
if check_feature "HddEnableDynamicPower"; then
	probe_modules syno_hddmon
fi

# load syno_smbus_hddmon
if check_feature "SMBusHddEnable"; then
	probe_modules syno_smbus_hddmon
fi

# load intel aesni
if check_feature "support_aesni_intel"; then
	probe_modules ablk_helper gf128mul lrw glue_helper aesni-intel
fi

# load lp3943
if check_feature "support_leds_lp3943"; then
	probe_modules i2c-i801 leds-lp3943
fi

CheckATMEGA1608Device()
{
	file=/sys/bus/i2c/devices/
	for f in "$file"*
	do
		name="$(cat "$f/name")"
		if [ "$name" = "atmega1608" ]; then
			if [ ! -d "$f/driver" ]; then
				# if any atmega1608 not is probed by driver, return false
				return 0
			fi
		fi
	done
	return 1
}

# load atemga1608
if check_feature "support_leds_atmega1608"; then
    probe_modules leds-atmega1608
	for retry_count in $(seq 1 3)
	do
		if CheckATMEGA1608Device; then
			echo "Failed to probe Atmega1608 device, Retry: $retry_count..."
			sleep 1
			modprobe --remove leds-atmega1608
			sleep 1
			probe_modules leds-atmega1608
		else
			break
		fi
	done
fi

CheckATMEGA1608SEG7Device()
{
	file=/sys/bus/i2c/devices/
	for f in "$file"*
	do
		name="$(cat "$f/name")"
		if [ "$name" = "atmega1608_seg7" ]; then
			if [ ! -d "$f/driver" ]; then
				# if any atmega1608_seg7 not is probed by driver, return false
				return 0
			fi
		fi
	done
	return 1
}

# load atemga1608 seg7
if check_feature "support_leds_atmega1608_seg7"; then
    probe_modules leds-atmega1608-seg7
	for retry_count in $(seq 1 3)
	do
		if CheckATMEGA1608SEG7Device; then
			echo "Failed to probe Atmega1608_seg7 device, Retry: $retry_count..."
			sleep 1
			modprobe --remove leds-atmega1608-seg7
			sleep 1
			probe_modules leds-atmega1608-seg7
		else
			break
		fi
	done
fi

# load IPMI
if check_feature "support_ipmi"; then
	probe_modules ipmi_msghandler ipmi_devintf ipmi_si
fi

# load ACM
if check_feature "support_acm"; then
	probe_modules cdc-acm
	mknod /dev/ttyACM0 c 166 0
fi

# load USB printer
if check_feature "support_printer"; then
	probe_modules usblp
fi

# load usb-storage and uas
if [ -f "/sys/bus/usb/syno_all_usb_uas_enabled" ]; then
	if check_feature "support_uasp"; then
		echo 1 > /sys/bus/usb/syno_all_usb_uas_enabled
	else
		echo 0 > /sys/bus/usb/syno_all_usb_uas_enabled
	fi
fi

modprobe usb-storage
if [ -f "/sys/bus/usb/syno_all_usb_uas_enabled" ]; then
	modprobe uas
fi
