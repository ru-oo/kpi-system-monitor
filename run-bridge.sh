#!/usr/bin/env bash
# ===========================================================================
#  KPI Monitor - Linux / WSL bridge
#  Reads CANable over SocketCAN (can0) and forwards the decoded physical data
#  to the iPad over UDP. Headless (no window). Ctrl-C to stop.
# ===========================================================================
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$DIR/build-linux/KpiProjectApp"
CAN_IF="${CAN_IF:-can0}"
BITRATE="${BITRATE:-500000}"

if [ ! -x "$APP" ]; then
  echo "Binary not found: $APP"
  echo "Build it first:"
  echo "  cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux"
  exit 1
fi

# Bring the CAN interface up if it isn't already (CANable candleLight = gs_usb -> can0).
# Bitrate MUST be set here (SocketCAN sets it on the interface, not via the app).
if ! ip link show "$CAN_IF" 2>/dev/null | grep -q "state UP"; then
  echo "Bringing up $CAN_IF @ ${BITRATE} bps (needs sudo) ..."
  sudo ip link set "$CAN_IF" up type can bitrate "$BITRATE" \
    || { echo "ERROR: could not bring up $CAN_IF. Is the CANable attached?"; \
         echo "       (WSL: attach it with usbipd first - see README-Linux.md)"; exit 1; }
fi
echo "CAN: $(ip -br link show "$CAN_IF" 2>/dev/null || echo "$CAN_IF (?)")"

# Talk to can0 via Qt's socketcan plugin; run as the UDP bridge.
export KPI_MODE=bridge
export KPI_CAN_PLUGIN=socketcan
export KPI_CAN_DEVICE="$CAN_IF"
echo "Bridge running: $CAN_IF -> UDP. Edit build-linux/config.json for bridge_host / ports. Ctrl-C to stop."
exec "$APP"
