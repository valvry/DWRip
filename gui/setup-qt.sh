#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Didik DW and contributors.
# Provision a local, relocatable Qt6 SDK for building dwrip-gui.
# This sandbox has no system package manager (apt/dnf/flatpak), only pip3 —
# so we fetch Qt via aqtinstall into gui/.qt (git-ignored).
#
# Usage:  sh gui/setup-qt.sh [QT_VERSION]
set -e

QT_VERSION="${1:-6.8.3}"
GUI_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ">> Installing aqtinstall (pip)..."
pip3 install --quiet aqtinstall

# aqt may land in a dir that's not on PATH; cover the common case.
export PATH="/var/data/python/bin:$HOME/.local/bin:$PATH"

echo ">> Downloading Qt ${QT_VERSION} (linux desktop gcc_64) into ${GUI_DIR}/.qt ..."
aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O "${GUI_DIR}/.qt"

echo ">> Done. Configure with:"
echo "   cmake -S gui -B gui/build -DCMAKE_PREFIX_PATH=${GUI_DIR}/.qt/${QT_VERSION}/gcc_64"
echo "   cmake --build gui/build -j"
