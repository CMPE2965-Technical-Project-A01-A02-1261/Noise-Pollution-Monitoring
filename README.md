# Noise Pollution Monitoring and Reporting System

A portable, battery-powered device that measures environmental noise once per second and sends each reading, with its time and location, over a cellular network to a web dashboard.

Capstone project for **Lovelyn Antiveros** and **Sadie Pretzlaff** (CMPE 2965, Group 7, NAIT).


## How it works

```
Device (STM32 + I2S microphone + XBee 3 LTE-M/NB-IoT)
   |  cellular, HTTP POST (batches of about 30 s)         \
   v                                                       Bluetooth
Server (PHP API + MariaDB)  --->  Web dashboard           mobile app
```

- A digital MEMS microphone (PUI Audio DMM-4026-B-I2S-EB-R) sends 24-bit samples over I2S to an STM32 NUCLEO-G0B1RE.
- The firmware applies an IEC 61672-1 A-weighting filter and calculates the equivalent continuous sound level (LAeq) once per second.
- A Digi XBee 3 Global modem sends the readings to the server in batches.
- The server stores the readings, groups them into recording sessions, and calculates LAeq, LAmax, LAmin, LA10, LA50 and LA90.
- The web dashboard shows a level chart, a map of sessions, and CSV export.
- A Bluetooth mobile app connects to the device for diagnostics and to set its time and location.

## Repository layout

| Folder | Contents |
|---|---|
| `device/` | STM32 firmware (C, HAL) for sound measurement and modem communication |
| `app/` | Bluetooth mobile app |
| `website/` | PHP API and web dashboard (HTML, CSS, JavaScript) |
| `documents/` | Reports, drafts, and figures |
| `.cpanel.yml` | Deployment settings for the school-hosted server |

## Status

Working prototype. Known limits:

- Readings have not been calibrated against a reference meter.
- GNSS did not get a position fix indoors, so test sessions use fixed coordinates.
- Traffic is sent over plain HTTP (not encrypted) in the prototype.
- No battery charger has been chosen yet.

## Secrets

The device key and the administrator key are kept out of the repository. The server reads them from a configuration file, and the firmware reads them from a local `device_secrets.h` file that is listed in `.gitignore`.

## Standards and references

- ISO 1996-2:2017, determination of environmental noise levels
- IEC 61672-1:2013, sound level meter specifications
- WHO Regional Office for Europe (2018), Environmental noise guidelines for the European Region
