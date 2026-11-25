# Remote Suite

Open **THE ZEN → REMOTE SUITE** to use a custom button layout with a remote from your SD card.

## Create a layout

1. Save a profile in **IR IDENTIFY**, or put a Flipper `.ir` file in `/ir/remotes/` on the SD card.
2. Open **REMOTE SUITE → CREATE REMOTE** and choose the Flipper file. Use **Left/Right** to scroll long names in a list.
3. Use **Up/Down** to choose a slot and **A** to assign a command. There are eight slots: the four directions and **A + direction**. Select **Unassigned** to clear a slot.
4. Select **SAVE LAYOUT** and give it a name. Each layout is saved under `/ir/suite/`. It contains a copy of the assigned commands, including RAW captures, so you can make multiple layouts from one source and use them independently.

## Use a layout

Choose **OPEN REMOTE** and select a saved layout. Press a direction to send its assigned signal. Hold **A** and press a direction to send a command from the second layer. The screen shows the active layer. Press **B** to return to Remote Suite.

## Hardware checks

- Import a saved IR Identify profile that includes both parsed and RAW commands.
- Assign commands to a plain direction and the same direction with the A modifier. Save the layout, reboot and verify both commands on the TV.
- Check that tapping A by itself sends no signal, unassigned directions send no signal, and pressing B from playback returns to Remote Suite.
- Exit Remote Suite with B and verify that POOM returns to **THE ZEN**. Open the original profile through **IR UNIV → FLIPPER FILE** and verify its commands.
