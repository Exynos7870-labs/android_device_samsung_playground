#!/bin/bash
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_COMMON=universal7870-common
VENDOR=samsung

MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "${MY_DIR}" ]]; then MY_DIR="${PWD}"; fi

ANDROID_ROOT="${MY_DIR}/../../.."

PATCHES_DIR="${ANDROID_ROOT}/vendor/${VENDOR}/${DEVICE_COMMON}-patches"
if [[ -d "${PATCHES_DIR}" ]]; then
    APPLY_SCRIPT="${PATCHES_DIR}/apply_patches.sh"
    if [[ -f "${APPLY_SCRIPT}" ]] && [[ -x "${APPLY_SCRIPT}" ]]; then
        echo "Applying common patches from ${PATCHES_DIR}..."
        "${APPLY_SCRIPT}"
    else
        echo "Warning: ${APPLY_SCRIPT} not found or not executable. This is likely and error. Skipping patches."
    fi
else
    echo "No patches directory found at ${PATCHES_DIR}, skipping. This might be intentionally."
fi