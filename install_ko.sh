#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p /lib/modules/$(uname -r)/kernel/misc
cp "$SCRIPT_DIR"/*.ko /lib/modules/$(uname -r)/kernel/misc && depmod -a

