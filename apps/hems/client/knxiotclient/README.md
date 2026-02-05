# KNX IoT Client

A Python client for discovering and interacting with KNX IoT devices on your network. This tool implements the KNX IoT Point API specification, supporting mDNS device discovery and secure CoAP communication with OSCORE encryption.

## Features

- **Device Discovery**: Find KNX IoT devices using mDNS/DNS-SD
- **Secure Communication**: OSCORE-protected CoAP requests for reading, writing, and observing
- **Resource Discovery**: Query device functional blocks (/.well-known/core)
- **Point Operations**: Read, write, and observe data points on KNX IoT devices
- **CBOR Decoding**: Automatic decoding of CBOR payloads to human-readable JSON format
- **Comprehensive Logging**: All operations logged to knxiotclient.log with detailed response information

## Prerequisites

- Python 3.11 or higher
- Poetry (Python dependency management)
- Network access to KNX IoT devices
- Security credentials file (Project Security.csv) for OSCORE communication

## Installation

For the minimal setup required to run the three commissioning scripts (`pv.bat`, `ev.bat`, `cem.bat`), see `SETUP_PV_EV_CEM.md`.

### Step 1: Install Poetry

We use [Poetry](https://python-poetry.org/) for dependency management.
Follow the [official installation guide](https://python-poetry.org/docs/#installation):

```cmd
pip install poetry
```

Configure Poetry to create a project-specific Python environment:

```cmd
poetry config virtualenvs.in-project true
poetry config cache-dir %USERPROFILE%\.poetry-cache
```


**What it does:**

1. Discovers the device by serial number using mDNS (10 second timeout)
2. Selects the best IPv6 address (prefers link-local `fe80::` addresses)
3. Loads OSCORE credentials from the security file
4. Reads `/.well-known/core` to discover all functional blocks
5. Iterates through each functional block to discover its properties
6. Reads all properties with metadata (data type, interfaces, descriptions)
7. Writes a timestamped device name to test write operations
8. Verifies the write by reading back the updated value

**Example Output:**

```text
======================================================================
KNX IoT: python based commissioning tool for CEM devices
======================================================================
Serial Number: 00fa10020c00

======================================================================
Device Discovery

Looking for device with serial number: 00fa10020c00
Timeout: 3.0 seconds

Device found: 00fa10020c00._knx._udp.local.
  Addresses:
  - 10.3.0.150
  - fe80::2e79:e148:451d:85d0
  - fd3e:f1b0:9b7:101:2cd7:57a2:3888:ba62
  - fd3e:f1b0:9b7:101:d5cf:dd90:8e20:1551
  Port: 53991
Using link-local IPv6 address: fe80::2e79:e148:451d:85d0
======================================================================
request and verify pase parameters
======================================================================
request and verify credential request
======================================================================
confirmV = ok
ms: 90327e27227b32abc473cf1761fe1f78
======================================================================
request and verify verification request
======================================================================
set temp toolkey via /auth/at
delete temp toolkey via /auth/at
set /.well-known/knx/ia 
set /.well-known/knx
set /fp/r 
set /fp/p 
set /fp/g 
set /fp/g 
set /auth/at 
set /auth/at 
======================================================================
commissioning completed
======================================================================
```



### Project Structure

The repository is structured as follows:

| Directory/File          | Description                                                                 |
| ----------------------- | --------------------------------------------------------------------------- |
| `.venv/`                | Python virtual environment (created by bootstrap script)                    |
| `.vscode/`              | VS Code editor configurations                                               |
| `data/`                 | Data directory for security credentials and OSCORE knxiotclient             |
| `knxiotclient/`         | Implementations for mdns, pase and coap handling                            |
| `script/`               | Utility                                                                     |
| `README.md`             | This documentation file                                                     |

## Troubleshooting

### No Devices Discovered

If discovery doesn't find any devices:

1. Verify devices are powered on and connected to the network
2. Check that mDNS/Bonjour is enabled on your network
3. Increase the timeout: `--timeout 10`
4. Check firewall settings (UDP port 5353 for mDNS)


## Contribute

We love contributions! Found a bug or would like to add a feature?
Improve the documentation or have additional comments?

Please help us and create a merge request. Follow the CONTRIBUTING.md document.
