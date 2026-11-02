#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
device_path="/dev/char_echo"
module_name="char_echo"

cd "$script_dir"

cleanup() {
	if [[ -e "$device_path" ]]; then
		sudo rm -f "$device_path"
	fi
	if lsmod | awk -v name="$module_name" '$1 == name { found = 1 } END { exit !found }'; then
		sudo rmmod "$module_name"
	fi
}

trap cleanup EXIT

make
sudo insmod "./${module_name}.ko"

major="$(awk -v name="$module_name" '$2 == name { print $1 }' /proc/devices)"
if [[ -z "$major" ]]; then
	echo "cannot find major number for ${module_name}" >&2
	exit 1
fi

sudo mknod "$device_path" c "$major" 0
sudo chmod 0666 "$device_path"

./char_echo_test
sudo dmesg | tail -n 20
