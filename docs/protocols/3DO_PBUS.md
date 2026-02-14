# 3DO Player Bus (PBUS) Controller Protocol

**Serial shift register daisy-chain system supporting hot-swappable multi-device input**

Implemented by Robert Dale Smith (2025)
Based on 3DO Opera hardware documentation and PBUS protocol specification

---

## Table of Contents

- [Overview](#overview)
- [Physical Layer](#physical-layer)
- [Protocol Architecture](#protocol-architecture)
- [Device Types & Bit Structures](#device-types--bit-structures)
  - [Control Pad (Joypad)](#control-pad-joypad)
  - [Analog Controller (Flightstick)](#analog-controller-flightstick)
  - [Mouse](#mouse)
  - [Lightgun](#lightgun)
  - [Arcade Controls](#arcade-controls)
- [Daisy Chain Mechanism](#daisy-chain-mechanism)
- [Device Identification](#device-identification)
- [Initialization & Hot-Swapping](#initialization--hot-swapping)
- [PIO Implementation](#pio-implementation)
- [Implementation Notes](#implementation-notes)

---

## Overview

The **3DO Player Bus (PBUS)** is a serial shift register protocol developed by The 3DO Company for the Opera hardware platform (1993).

### Key Characteristics

- **Serial shift register**: Clock-synchronized bit-by-bit transmission
- **Daisy-chainable**: Up to ~56 devices theoretically supported (448 bits per field)
- **Hot-swappable**: Device IDs transmitted with every packet enable dynamic reconfiguration
- **Self-identifying**: Each device transmits its type code in every response
- **Bidirectional**: Console sends data out while simultaneously reading extension devices
- **Zero-terminated**: String of zeros marks end of daisy chain

### Historical Context

The PBUS protocol is described in patent [WO1994010636A1](https://patents.google.com/patent/WO1994010636A1) (filed 1992, published 1994), designed for 3DO Interactive Multiplayer hardware. The ID encoding exploits physical impossibility (joysticks can't press up+down simultaneously) to distinguish joypads from other device types without dedicated ID bytes.

---

## Physical Layer

### Connector

The 3DO uses a 9-pin (DE-9) controller connector. See the [FZ-1 Technical Guide](https://3dodev.com) for model-specific pinout details.

### RP2040 GPIO Mapping

For the Waveshare RP2040 Zero adaptation (matching [USBTo3DO](https://github.com/FCare/USBTo3DO)):

| GPIO | Signal | Direction | Description |
|------|--------|-----------|-------------|
| 2 | CLK | Input | Clock from 3DO console |
| 3 | DATA_OUT | Output | Serial data to 3DO console |
| 4 | DATA_IN | Input | Serial data from daisy chain |
| 5 | CS_CTRL | Control | Chip select / control signal |

### Electrical Characteristics

- **Logic levels**: TTL compatible (0V / 5V)
- **Field rate**: 60 Hz (NTSC) / 50 Hz (PAL)
- **Data timing**: Synchronized to CLK edges

### Signal Behavior

```
CLK:       ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐     (Clock from console)
           ┘ └─┘ └─┘ └─┘ └─┘ └───

DATA_OUT:  ──█─█─█─█─█─█─█─█─────  (Console sends controller data)

DATA_IN:   ──────█─█─█─█─█─█─────  (Simultaneous read from extension)
```

**Bidirectional Operation**:
- Console shifts out controller data to first device
- Simultaneously reads data from end of daisy chain
- Each device in chain shifts in data while shifting out to next device
- One clock cycle = one bit transmitted/received

---

## Protocol Architecture

### Data Structure

All PBUS transmissions follow this structure:

```
[Device ID + data] [Device ID + data] ... [0x00 terminator]
```

**Key Protocol Features**:

1. **Minimum 8 bits per device**: Each device must transmit at least 1 byte
2. **Maximum 448 bits per field**: Total bandwidth limit (~56 bytes)
3. **ID-first transmission**: Device type always sent before data
4. **Zero-string termination**: Multiple 0x00 bytes indicate no more devices

### Bit Transmission Order

- **MSB first**: Most significant bit transmitted first
- **Active-HIGH encoding**: 1 = pressed/active, 0 = not pressed

---

## Device Types & Bit Structures

### Control Pad (Joypad)

**Total length**: 16 bits (2 bytes)

```
Byte 0 (MSB first): [A][Left][Right][Up][Down][ID2][ID1][ID0]
  Bit 7: A button
  Bit 6: D-pad Left
  Bit 5: D-pad Right
  Bit 4: D-pad Up
  Bit 3: D-pad Down
  Bits 2-0: Device ID (always non-zero for joypad, e.g. 0b100)

Byte 1: [Tail1][Tail0][L][R][X][P][C][B]
  Bits 7-6: Tail (0b00)
  Bit 5: L shoulder
  Bit 4: R shoulder
  Bit 3: X button
  Bit 2: P button (Play/Pause)
  Bit 1: C button
  Bit 0: B button
```

**Button States** (active-HIGH):
- 1 = Button pressed / D-pad direction active
- 0 = Button released / D-pad neutral

**ID Detection**: Upper 2 bits of byte 0's ID nibble (bits 4-3) CANNOT both be 00 because that would mean both Up and Down are pressed simultaneously, which is physically impossible on a d-pad. This constraint reserves the 00XX pattern for non-joypad devices.

### Analog Controller (Flightstick)

**Total length**: 72 bits (9 bytes)

```
Byte 0: 0x01          (ID byte 1)
Byte 1: 0x7B          (ID byte 2)
Byte 2: 0x08          (ID byte 3 / length)
Bytes 3-6: Analog axes (4 x 8-bit)
Byte 7: [Left][Right][Down][Up][C][B][A][FIRE]
Byte 8: [Tail:4][R][L][X][P]
```

**Byte 7 Button Mapping**:
- Bit 7: D-pad Left
- Bit 6: D-pad Right
- Bit 5: D-pad Down
- Bit 4: D-pad Up
- Bit 3: C button
- Bit 2: B button
- Bit 1: A button
- Bit 0: FIRE (trigger)

**Byte 8 Button Mapping**:
- Bits 7-4: Tail (0x0)
- Bit 3: R shoulder
- Bit 2: L shoulder
- Bit 1: X button
- Bit 0: P button (Play/Pause)

### Mouse

**Total length**: 32 bits (4 bytes)

```
Byte 0: 0x49              (Mouse ID)
Byte 1: [Left][Middle][Right][Shift][DY_upper:4]
Byte 2: [DX_upper:2][DY_lower:6]
Byte 3: DX_lower (8 bits)
```

**Button Layout** (byte 1, bits 7-4):
- Bit 7: LEFT button
- Bit 6: MIDDLE button
- Bit 5: RIGHT button
- Bit 4: SHIFT button (modifier)

**Delta Encoding**:
- **Y-axis**: 10-bit signed (-512 to +511)
  - Upper 4 bits in byte 1 (bits 3-0), lower 6 bits in byte 2 (bits 5-0)
- **X-axis**: 10-bit signed (-512 to +511)
  - Upper 2 bits in byte 2 (bits 7-6), lower 8 bits in byte 3
- Negative values use two's complement
- Deltas are relative motion since last frame

**Conversion** (from `3do_device.c`):
```c
// Delta Y (10-bit signed)
int16_t dy = ((byte1 >> 4) & 0x0F) << 6 | (byte2 & 0x3F);
if (dy & 0x200) dy |= 0xFC00;

// Delta X (10-bit signed)
int16_t dx = ((byte2 >> 6) & 0x03) << 8 | byte3;
if (dx & 0x200) dx |= 0xFC00;
```

### Lightgun

**Total length**: 32 bits (4 bytes)

```
Byte 0: 0x4D          (Lightgun ID)
Byte 1: Counter[19:12] (Timing counter, upper 8 bits)
Byte 2: Counter[11:4]  (Timing counter, middle 8 bits)
Byte 3: [Counter[3:0]][Line[4:0]][Buttons]
```

**Timing Counter** (20-bit):
- Measures time from field start to beam detection
- NTSC defaults: XSCANTIME=1030, TIMEOFFSET=-12835

**Line Pulse Count** (5-bit):
- Number of scanlines where beam was detected
- NTSC default: YSCANTIME=12707

**Position Calculation**:
```c
x_position = (counter * XSCANTIME) + TIMEOFFSET;
y_position = line_count * YSCANTIME;
```

### Arcade Controls (Silly Control Pad)

**Total length**: 16 bits (2 bytes)

```
Byte 0: 0xC0          (Arcade ID)
Byte 1: [P1_Coin][P1_Start][unused][P2_Coin][unused][P2_Start][unused][Service]
```

**Byte 1 Button Mapping**:
- Bit 7: Player 1 Coin
- Bit 6: Player 1 Start
- Bit 5: unused
- Bit 4: Player 2 Coin
- Bit 3: unused
- Bit 2: Player 2 Start
- Bit 1: unused
- Bit 0: Service (arcade maintenance)

Used for Orbatak arcade cabinet integration.

---

## Daisy Chain Mechanism

### Physical Topology

```
┌─────────┐         ┌────────┐         ┌────────┐         ┌────────┐
│ Console │────────▶│ USB    │────────▶│ 3DO    │────────▶│ 3DO    │
│         │◀────────│ Adapter│◀────────│ Pad #1 │◀────────│ Pad #2 │
└─────────┘         └────────┘         └────────┘         └────────┘
     │                   │                   │                   │
   CLK ────────────────────────────────────────────────────────▶
  D_OUT ──█████──────────────────────────────────────────────▶
  D_IN  ◀────────────────────────────────────────────█████───
```

### Data Flow

**Console Perspective**:
1. Console shifts out controller data on DATA_OUT (USB adapter data)
2. Simultaneously samples DATA_IN for extension device data
3. Each clock cycle: send 1 bit, receive 1 bit
4. After 8-448 bits, complete device data captured

**Passthrough Buffering**:
- USB adapter must buffer extension data from one field
- Next field: send USB data + buffered extension data
- This creates 1-frame latency for extension controllers

---

## Device Identification

### ID Encoding Scheme

**Joypad ID Rules**:
- Upper 2 bits of the ID nibble (bits 4-3 of byte 0) CANNOT be 00
- Physical impossibility: d-pad can't press up+down simultaneously
- Valid patterns: 01XX, 10XX, 11XX
- This reserves 00XX pattern for other devices

**Other Device IDs**:
- 0x01 0x7B 0x08: Flightstick (3-byte signature)
- 0x49: Mouse
- 0x4D: Lightgun
- 0xC0: Arcade (Silly Control Pad)

### ID Detection Algorithm

From `parse_extension_controllers()` in `3do_device.c`:

```c
uint8_t byte0 = buffer[offset];
uint8_t id_nibble = (byte0 >> 4) & 0x0F;

if ((id_nibble & 0b1100) != 0) {
    // Joypad: upper 2 bits of nibble are non-zero
    device_type = DEVICE_JOYPAD;
    bytes_to_read = 2;
} else if (byte0 == 0x01 &&
           buffer[offset+1] == 0x7B && buffer[offset+2] == 0x08) {
    device_type = DEVICE_FLIGHTSTICK;
    bytes_to_read = 9;
} else if (byte0 == 0x49) {
    device_type = DEVICE_MOUSE;
    bytes_to_read = 4;
} else if (byte0 == 0x4D) {
    device_type = DEVICE_LIGHTGUN;
    bytes_to_read = 4;
} else if (byte0 == 0xC0) {
    device_type = DEVICE_ARCADE;
    bytes_to_read = 2;
}
```

### End-of-Chain Detection

Multiple 0x00 bytes indicate no more devices in the chain.

---

## Initialization & Hot-Swapping

### System Initialization Sequence

1. Console sends "string of zeros" on DATA_OUT
2. Devices self-configure (no handshake required)
3. Console clocks out data and reads device responses
4. Portfolio OS loads appropriate drivers based on device IDs

### Hot-Swap Support

- **Device Addition**: New device appears in next frame's data stream, identified by its ID code
- **Device Removal**: Device stops responding, earlier terminator detected
- No console reset required for either case

### Driver Mapping

Portfolio OS loads drivers by device ID:
- 0x01: StickDriver (flightstick)
- 0x49: MouseDriver (cport49.rom)
- 0x4D: LightGunRom
- 0xC0: Arcade controls

---

## PIO Implementation

### RP2040 PIO State Machine

From `output.pio`:
```asm
.program output

.define CLK_PIN 2

public entry_point:
  wait 0 gpio CLK_PIN           ; Wait for clock low
.wrap_target
start:
  out pins, 1                   ; Shift out 1 bit to DATA_OUT
  wait 1 gpio CLK_PIN           ; Wait for clock high
  wait 0 gpio CLK_PIN           ; Wait for clock low
  in pins, 1                    ; Shift in 1 bit from DATA_IN
.wrap
```

### DMA Architecture

Dual-channel DMA for simultaneous TX/RX:

```c
// Channel 0: OUTPUT - Send data to console
channel_config_set_transfer_data_size(&cfg_out, DMA_SIZE_8);
channel_config_set_read_increment(&cfg_out, true);
channel_config_set_dreq(&cfg_out, pio_get_dreq(pio, sm, true));

// Channel 1: INPUT - Receive extension data
channel_config_set_transfer_data_size(&cfg_in, DMA_SIZE_8);
channel_config_set_write_increment(&cfg_in, true);
channel_config_set_dreq(&cfg_in, pio_get_dreq(pio, sm, false));
```

**Buffer**: `controller_buffer[201]` — padded beyond the 56-byte max (448 bits) for DMA alignment.

---

## Implementation Notes

### Common Pitfalls

1. **Active-HIGH encoding**: PBUS uses 1=pressed (opposite of PCEngine). Invert when porting from other protocols.

2. **Joypad is 2 bytes, not 1**: Each joypad transmits 16 bits. Don't skip only 1 byte when parsing.

3. **ID detection order**: Check joypad first (most common), then flightstick (3-byte signature), then single-byte IDs.

4. **End marker**: A single 0x00 byte is NOT end-of-chain. Must verify multiple consecutive zero bytes.

5. **Extension latency**: Extension controllers have 1-frame delay due to passthrough buffering.

6. **Buffer size**: Allocate full 201-byte buffer even if unused. Prevents overflow with many devices connected.

---

## Acknowledgments

- **[3dodev.com](https://3dodev.com)**: PBUS protocol specification and Opera hardware documentation
- **[FCare's USBTo3DO](https://github.com/FCare/USBTo3DO)**: Original USB-to-3DO adapter that pioneered USB controller conversion for the 3DO
- **[SNES23DO](https://github.com/RobertDaleSmith/snes23do)**: SNES-to-3DO adapter with extension controller parsing

## References

- **3DO PBUS Specification**: [3dodev.com/documentation/hardware/opera/pbus](https://3dodev.com/documentation/hardware/opera/pbus)
- **Patent WO1994010636A1**: Player Bus Apparatus and Method (1994)
- **USBTo3DO**: [github.com/FCare/USBTo3DO](https://github.com/FCare/USBTo3DO)

### Implementation

- **Joypad 3DO Module**: `src/native/device/3do/` — Full PBUS implementation with extension detection
- **PIO State Machines**: `src/native/device/3do/output.pio` — Clock-synchronized bidirectional I/O
- **Extension Parsing**: `src/native/device/3do/3do_device.c` — `parse_extension_controllers()`
