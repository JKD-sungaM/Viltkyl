Import("env")

import time
import serial
from serial.tools import list_ports

# Kända USB-ID för Nano ESP32 / ESP32-S3 varianter.
ARDUINO_RUNTIME_HWID = "VID:PID=2341:0070"
ESP_BOOTLOADER_HWID = "VID:PID=303A:1001"


def find_port_by_hwid(ports, hwid):
    for port in ports:
        if hwid in (port.hwid or ""):
            return port
    return None


def touch_runtime_port(runtime_device):
    try:
        ser = serial.Serial(runtime_device, baudrate=1200, timeout=0.2)
        ser.dtr = False
        ser.rts = False
        ser.close()
        return True
    except Exception:
        return False


def rescan_ports(delay_seconds=1.0):
    time.sleep(delay_seconds)
    return list(list_ports.comports())


def select_ports():
    ports = list(list_ports.comports())

    runtime_port = find_port_by_hwid(ports, ARDUINO_RUNTIME_HWID)
    bootloader_port = find_port_by_hwid(ports, ESP_BOOTLOADER_HWID)

    monitor_port = None
    upload_port = None

    if runtime_port:
        monitor_port = runtime_port.device

    # Om endast runtime-port hittas, trigga bootloader och försök hitta upload-port igen.
    if runtime_port and not bootloader_port:
        if touch_runtime_port(runtime_port.device):
            rescanned = rescan_ports(1.2)
            bootloader_port = find_port_by_hwid(rescanned, ESP_BOOTLOADER_HWID)

    if bootloader_port:
        upload_port = bootloader_port.device

    return monitor_port, upload_port, runtime_port, bootloader_port


monitor_port, upload_port, runtime_port, bootloader_port = select_ports()

if monitor_port:
    env.Replace(MONITOR_PORT=monitor_port)

if upload_port:
    env.Replace(UPLOAD_PORT=upload_port)

if monitor_port or upload_port:
    runtime_text = runtime_port.device if runtime_port else "none"
    bootloader_text = bootloader_port.device if bootloader_port else "none"
    print(
        f"[auto_port] runtime={runtime_text}, bootloader={bootloader_text}, "
        f"monitor={monitor_port or 'auto'}, upload={upload_port or 'auto'}"
    )
else:
    print("[auto_port] No matching ESP32 port found; PlatformIO default detection will be used")
