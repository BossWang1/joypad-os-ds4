# Console Protocol Documentation

Technical documentation of the retro console communication protocols implemented in Joypad.

## Available Protocol Documentation

### ✅ [Nuon Polyface Protocol](NUON_POLYFACE.md)
**Status**: Complete
**Reverse-Engineered**: 2022-2023
**Significance**: First open-source documentation

The Nuon Polyface controller protocol was previously undocumented publicly. Based on hardware analysis, SDK research, and iterative testing.

### ✅ [PCEngine / TurboGrafx-16 Protocol](PCENGINE.md)
**Status**: Complete
**Implemented**: 2022-2023
**Significance**: Comprehensive multitap and mouse implementation reference

While the basic PCEngine controller protocol is documented in the community, this reference provides detailed coverage of:
- Multitap scanning mechanism (5-player support)
- Mouse delta accumulation strategy
- 6-button and 3-button mode implementations
- RP2040 PIO state machine architecture
- Dual-core coordination for timing-critical output

### ✅ [3DO Player Bus (PBUS) Protocol](3DO_PBUS.md)
**Status**: Complete
**Implemented**: 2025
**Significance**: Comprehensive PBUS reference with daisy-chain and device type documentation

Covers the 3DO PBUS serial shift register protocol:
- Device identification and daisy-chain mechanics
- Bit structures for all device types (joypad, flightstick, mouse, lightgun)
- Hot-swapping and initialization
- RP2040 PIO implementation

### ✅ [GameCube Joybus Protocol](GAMECUBE_JOYBUS.md)
**Status**: Complete
**Implemented**: 2022-2025
**Significance**: Reverse-engineered keyboard protocol + comprehensive profile system

While the basic joybus protocol is documented, this reference provides:
- Reverse-engineered GameCube keyboard protocol (0x54 command, keycode mappings, checksum algorithm)
- Profile system with flash persistence (SSBM, MKWii, Fighting, SNES configs)
- Joybus PIO timing implementation (130MHz overclocking requirement)
- Trigger logic for different controller types
- Dual-core coordination for timing-critical output

## Documentation Standards

Each protocol document should include:

### 1. **Overview Section**
- Historical context
- Significance (original documentation or reverse-engineered)
- Key characteristics
- Physical layer description

### 2. **Protocol Details**
- Packet structure with bit-level diagrams
- Command reference with hex values
- State machines and enumeration sequences
- Timing diagrams and requirements

### 3. **Implementation Notes**
- Hardware requirements
- PIO programs (if applicable)
- Dual-core considerations
- Known limitations

### 4. **Practical Examples**
- Real packet captures
- Common command sequences
- Button/analog encoding tables
- CRC/checksum algorithms

### 5. **Appendices**
- Quick reference tables
- Bit field maps
- Acknowledgments
- References

## Document Template

Use this structure for new protocol documentation:

```markdown
# [Console Name] [Protocol Name] Protocol

**[Status of prior documentation]**

Reverse-engineered/Documented by [Author] ([Year])

[Brief description of significance]

---

## Table of Contents
[...]

## Overview
[...]

## Physical Layer
[...]

## Packet Structure
[...]

## Protocol State Machine
[...]

## Command Reference
[...]

## [Console-Specific Sections]
[...]

## Implementation Notes
[...]

## Acknowledgments
[...]

## References
[...]
```

## Contributing

When adding new protocol documentation:

1. **Research thoroughly** - Include citations for existing documentation
2. **Credit sources** - Acknowledge prior work and community contributions
3. **Provide examples** - Real-world packet captures and test cases
4. **Document discoveries** - Highlight reverse-engineered insights
5. **Maintain consistency** - Follow the established format

## Related Resources

### External Protocol Documentation

- **GameCube Joybus**: [joybus-pio](https://github.com/JonnyHaystack/joybus-pio)
- **N64 Controller**: [N64brew](https://n64brew.dev)
- **PCEngine**: [pce-devel/PCE_Controller_Info](https://github.com/pce-devel/PCE_Controller_Info)
- **Nuon**: No prior public documentation (see NUON_POLYFACE.md)

### Retro Console Communities

- **[Nuon-Dome](https://www.nuon-dome.com/)**: Nuon homebrew community
- **[N64brew](https://n64brew.dev)**: Nintendo 64 development

## License

All protocol documentation in this directory is licensed under Apache License 2.0, matching the Joypad project license.

