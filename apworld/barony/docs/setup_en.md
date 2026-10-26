# Barony Multiworld Setup Guide

## What you need

- Barony on Steam
- The Archipelago build of Barony (see BUILDING.md in the Barony AP project)
- Archipelago 0.6.7 or newer, with `barony.apworld` installed

## Installing the APWorld

Double-click `barony.apworld`, or copy it into the `custom_worlds` folder of your Archipelago install.

## Making your YAML

Open the Archipelago Launcher, choose **Generate Template Options**, and edit `Barony.yaml`.
The main choices are your goal, how often floors give checks, Stair Keys, floor shuffle, lives and traps.

## Connecting

Put `archipelago.cfg` next to your Archipelago `barony.exe`:

```
server=archipelago.gg:38281
slot=YourSlotName
password=
```

The game connects at the main menu. You can also type `/ap_connect archipelago.gg:38281 YourSlotName`
into the in-game chat.

Connect before you start or continue a run, so floor shuffle is in place when the first floor loads.

## Useful chat commands

- `/ap_status`: connection, Stair Keys, lives, kills and queued traps
- `/ap_say message`: chat with the room
