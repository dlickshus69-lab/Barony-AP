# Barony Multiworld Setup Guide

## What you need

- Barony on Steam
- The Archipelago build of Barony (see BUILDING.md in the Barony AP project)
- Archipelago 0.6.7 or newer, with `barony.apworld` installed

## Installing the APWorld

Double-click `barony.apworld`, or copy it into the `custom_worlds` folder of your Archipelago install.

## Making your YAML

Open the Archipelago Launcher, choose **Generate Template Options**, and edit `Barony.yaml`.
The main choices are your goal, how often floors give checks, Floor Keys, floor shuffle, lives and traps.

## Connecting

Start Barony. The tracker window opens with it. Type the room's **port** (the digits after
`archipelago.gg:`) and your **name** (your slot name), then press **Enter** or **Connect**. The server
box is already `archipelago.gg`, and there's no password box: rooms without a password don't need one.
The tracker remembers what you typed for next time, so usually you only change the port.

Without the tracker, put `archipelago.cfg` next to your Archipelago `barony.exe`:

```
server=archipelago.gg:38281
slot=YourSlotName
password=
```

and the game connects at the main menu. You can also type `/ap_connect archipelago.gg:38281 YourSlotName`
into the in-game chat.

Connect before you start or continue a run, so floor shuffle is in place when the first floor loads.

Barony's holiday themes (Halloween in October, Christmas in December) are turned off, so you always
hear the normal soundtrack. Add `holiday_theme=1` to `archipelago.cfg` to keep them.

## Useful chat commands

- `/ap_status`: connection, lives, kills and queued traps
- `/ap_say message`: chat with the room
