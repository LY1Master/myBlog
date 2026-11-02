#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
module_dir="$script_dir/module"
test_dir="$script_dir/test"
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

# 两个独立 Makefile：前者调用 Linux Kbuild，后者只编译用户态测试程序。
make -C "$module_dir"
make -C "$test_dir"

sudo insmod "${module_dir}/${module_name}.ko"

major="$(awk -v name="$module_name" '$2 == name { print $1 }' /proc/devices)"
if [[ -z "$major" ]]; then
	echo "cannot find major number for ${module_name}" >&2
	exit 1
fi

sudo mknod "$device_path" c "$major" 0
sudo chmod 0666 "$device_path"

"${test_dir}/char_echo_test"
sudo dmesg | tail -n 20
