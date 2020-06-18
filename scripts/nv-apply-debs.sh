#!/bin/bash
set -e

#L4T_BOOTLOADER_DIR="${L4T_DIR}/bootloader"

#if [ -e "${L4T_BOOTLOADER_DIR}/nv_boot_control.conf" ]; then
#	# copy nv_boot_control.conf to rootfs to support bootloader
#	# and kernel updates
#	echo "Copying nv_boot_control.conf to /etc"
#	cp "${L4T_BOOTLOADER_DIR}/nv_boot_control.conf" \
#	"/etc/"
#fi

function AddDebsList {
	local category="${1}"

	if [ -z "${category}" ]; then
		echo "Category not specified"
		exit 1
	fi

	for deb in "${category}"/*.deb; do
		deb_name=$(basename ${deb})
		if [[ "${deb_name}" == "nvidia-l4t-ccp"* ]]; then
			pre_deb_list+=("${category}/${deb_name}")
#      echo "Skipping "${category}/${deb_name}""
		else
			deb_list+=("${category}/${deb_name}")
		fi
	done
}

# pre_deb_list includes Debian packages which must be installed before
# deb_list
pre_deb_list=()
deb_list=()

AddDebsList "${L4T_NV_TEGRA_DIR}/l4t_deb_packages"
AddDebsList "${L4T_DIR}/tools"
AddDebsList "${L4T_DIR}/kernel"

if [ "${#deb_list[@]}" -eq 0 ]; then
	echo "No packages to install. There might be something wrong"
	exit 1
fi

echo "Installing BSP Debian packages"
if [ "${#pre_deb_list[@]}" -ne 0 ]; then
	dpkg -i --path-include="/usr/share/doc/*" "${pre_deb_list[@]}"
fi
dpkg -i --path-include="/usr/share/doc/*" "${deb_list[@]}"
popd

echo "L4T BSP package installation completed!"
exit 0
