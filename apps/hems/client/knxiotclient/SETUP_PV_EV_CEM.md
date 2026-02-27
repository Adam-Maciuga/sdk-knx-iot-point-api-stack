# Run the scripts for pv, ev and cem

This guide is the minimum required setup to run the three commissioning scripts on another Windows machine:


## 1) Prerequisites

- Windows 10/11
- Python 3.10 <-> 3.12.3 (3.11 recommended)
- Git (only if cloning the repo)

## 2) Get the project

Clone or copy the repo to the target machine, for example:

```
C:\temp
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

If `poetry install` fails with a permission error on the Poetry cache folder, set a project-local cache:

```cmd
poetry config cache-dir "C:\temp\.poetry-cache"
```

## 4) Install runtime dependencies only

From the repo root:

```cmd
cd C:\temp
poetry install --only main
```

## 5) Start Windows PowerShell

```PS
PS C:\temp> dir


    Directory: C:\temp


Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
d-----         2/20/2026  10:38 AM                .venv
d-----         2/20/2026  10:35 AM                .vscode
d-----         2/20/2026  10:35 AM                knxiotclient
d-----         2/20/2026  10:35 AM                script
-a----         2/18/2026   2:02 PM             29 .env
-a----         2/18/2026   2:02 PM          90934 poetry.lock
-a----         2/18/2026   2:02 PM            629 pyproject.toml
-a----         2/18/2026   2:02 PM           5159 README.md
-a----         2/18/2026   2:02 PM           1398 SETUP_PV_EV_CEM.md
```

## 5) Start (in PS) the virtual environment

```PS
PS C:\temp> .\.venv\Scripts\Activate.ps1
```

## 6) Start in the virtual environment the scripts

```(knxiotclient-py3.12)
(knxiotclient-py3.12) PS C:\temp> .venv\Scripts\python.exe c:\temp/script/pv.py 00fa10020b00
..
(knxiotclient-py3.12) PS C:\temp> .venv\Scripts\python.exe c:\temp/script/cem.py 00fa10020c00
..
(knxiotclient-py3.12) PS C:\temp> .venv\Scripts\python.exe c:\temp/script/ev.py 00fa10020d00
..
```

## Notes / Troubleshooting

- The scripts rely on mDNS discovery and IPv6. The target machine must be on the same network as the KNX IoT devices.
- Link-local IPv6 addresses (`fe80::...`) are preferred when available.
