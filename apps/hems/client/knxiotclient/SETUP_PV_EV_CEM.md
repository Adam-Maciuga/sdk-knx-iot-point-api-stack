# Setup: pv.bat / ev.bat / cem.bat

This guide is the minimum required setup to run the three commissioning scripts on another Windows machine:

- `script\pv.bat`
- `script\ev.bat`
- `script\cem.bat`

## 1) Prerequisites

- Windows 10/11
- Python 3.10�3.12.3 (3.11 recommended)
- Git (only if cloning the repo)

## 2) Get the project

Clone or copy the repo to the target machine, for example:

```
C:\dev\knxiotclient
```

## 3) Install Poetry

Use the Python that you want the project to run with:

```cmd
pip install poetry
```

Configure Poetry to keep the virtual environment inside the repo (required by these scripts):

```cmd
poetry config virtualenvs.in-project true
```

## 4) Install runtime dependencies only

From the repo root:

```cmd
cd C:\dev\SBT\knxiotclient
poetry install --only main
```

## 5) Run the scripts

```cmd
.\script\pv.bat <serial_number>
.\script\ev.bat <serial_number>
.\script\cem.bat <serial_number>
```

## Notes / Troubleshooting

- The scripts rely on mDNS discovery and IPv6. The target machine must be on the same network as the KNX IoT devices.
- Link-local IPv6 addresses (`fe80::...`) are preferred when available.
