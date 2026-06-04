# CLI Tool

!!! warning "Coming soon"
    The USMP CLI tool is under active development.

The `usmp` CLI tool lets you interact with USMP devices on your LAN
directly from the terminal — like SSH for IoT devices.

## Planned commands

```bash
# Scan LAN for USMP devices
usmp scan

# Connect to a device (interactive shell)
usmp connect aa:bb:cc:dd:ee:ff

# Stream logs from a device
usmp logs aa:bb:cc:dd:ee:ff

# Send a one-shot command
usmp send aa:bb:cc:dd:ee:ff "reboot"

# Monitor all devices
usmp monitor
```

## Planned output

```
$ usmp scan
Scanning LAN for USMP devices...
  aa:bb:cc:dd:ee:ff  192.168.1.60  ESP32  USMP v0.1  online
  aa:bb:cc:dd:ee:00  192.168.1.61  ESP32  USMP v0.1  online

$ usmp connect aa:bb:cc:dd:ee:ff
Connecting to aa:bb:cc:dd:ee:ff (192.168.1.60)...
[USMP] Handshake complete — session a3f1b2c4
> get_sensor
temperature: 23.4C
> exit
```

## Installation (when available)

```bash
pip install usmp-cli

```
