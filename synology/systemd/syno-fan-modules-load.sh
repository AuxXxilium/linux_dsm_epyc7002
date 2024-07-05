#!/bin/sh

SYNOINFO_DEF="/etc.defaults/synoinfo.conf"

CheckADTDevice()
{
	file=/sys/class/hwmon/
	for f in "$file"*
	do
		if [ -d "$f/device" ]; then
			name="$(cat "$f/device/name")"
			if [ "$name" = "adt7490" ];then
				return 1
			fi
		fi
	done
	return 0
}

SYNOLoadAdt7490()
{
	modprobe i2c-i801
	modprobe adt7475
	for retry_count in $(seq 1 3)
	do
		if CheckADTDevice; then
			echo "Can not detect ADT device, Retry: $retry_count..."
			sleep 1
			modprobe --remove i2c-i801
			modprobe --remove adt7475
			sleep 1
			modprobe i2c-i801
			modprobe adt7475
		else
			break
		fi
	done
}

# shellcheck disable=SC2125 # FIXME
findADTPath()
{
	ret=0
	unset ADTDIRS
	file=/sys/class/i2c-dev/i2c-*/device/*-002[c-e]
	for f in $file
	do
		if [ ! -f "$f/name" ]; then
			continue
		fi
		name="$(cat "$f/name")"
		if [ "$name" = "adt7490" ];then
			ADTDIRS="${ADTDIRS} $f"
			ret=1
		fi
	done
	return ${ret}
}

# here we assume adt master will always 0x2e, because due to adt7490's spec, only 0x2e address can be fixed
# shellcheck disable=SC2144 # FIXME
# shellcheck disable=SC2125 # FIXME
findADTMaster()
{
	unset ADTMASTER
	adtmasters=/sys/class/i2c-dev/i2c-*/device/*-002e

	if [ -f /sys/class/i2c-dev/i2c-*/device/*-002e/name ]; then
		device2e="$(cat /sys/class/i2c-dev/i2c-*/device/*-002e/name)"
		if [ "$device2e" = "adt7490" ]; then
			adtmasters=/sys/class/i2c-dev/i2c-*/device/*-002e
		fi
	elif [ -f /sys/class/i2c-dev/i2c-*/device/*-002c/name ]; then
		device2c="$(cat /sys/class/i2c-dev/i2c-*/device/*-002c/name)"
		if [ "$device2c" = "adt7490" ]; then
			adtmasters=/sys/class/i2c-dev/i2c-*/device/*-002c
		fi
	fi

	for f in $adtmasters
	do
		# use only 1 master
		ADTMASTER="$f"
		break
	done
}

SoftLink7490fanInput()
{
	adtfanTmpPath="/tmp/ADTFanPath/"
	findADTPath
	# no adt path found
	if [ 1 -ne $? ]; then
		return
	fi
	findADTMaster
	if [ -z "${ADTMASTER}" ]; then
		return
	fi
	/bin/mkdir -p "${adtfanTmpPath}"
	# soft link master adt7490
	masterfiles=$(ls -d "${ADTMASTER}"/hwmon/hwmon*/*)
	for masterfile in ${masterfiles}
	do
		ln -s "${masterfile}" "${adtfanTmpPath}"/
	done
	# assume other fan input comes from 5
	fanCnt=5
	# soft link slave adt7490
	for ADTDIR in ${ADTDIRS}
	do
		# skip master itself
		if [ "${ADTDIR}" = "${ADTMASTER}" ]; then
			continue
		fi
		slaveFans=$(ls "${ADTDIR}"/hwmon/hwmon*/fan*_input)
		for slavefan in ${slaveFans}
		do
			ln -s "${slavefan}" "${adtfanTmpPath}/fan${fanCnt}_input"
			fanCnt=$((fanCnt + 1))
		done
	done
}


SUPPORT_ADT1475=$(get_key_value $SYNOINFO_DEF supportadt7490)
if [ "$SUPPORT_ADT1475" = "yes" ]; then
	SYNOLoadAdt7490
	SoftLink7490fanInput
fi
