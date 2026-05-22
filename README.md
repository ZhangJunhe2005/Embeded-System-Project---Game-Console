# Game Console - STM32L476 Multi-Game System

A complete game console implementation on STM32L476RG Nucleo board featuring three distinct games with shared hardware abstractions and rendering infrastructure.

## Overview

This project implements a multi-game console with a modular architecture:
- **Game 1 - Apex Training**: First-person aiming training simulation with robot targets
- **Game 2 - Turbo Overdrive**: Pseudo-3D racing game with procedural track generation
- **Game 3 - Silk Knight: Abyss Patrol**: Pixel-art platformer with dynamic enemy AI

All games share common infrastructure:
- **Hardware**: PS2 mouse/keyboard input, ST7789V2 LCD display (240×240), WS2812B RGB LED strip
- **Audio**: PWM-based buzzer with frequency control
- **Rendering**: Optimized line-by-line LCD rendering with DMA acceleration
- **Assets**: QSPI flash storage (W25Q128) for large sprite sheets and backgrounds

## Project Structure

```
.
├── game_1/               Apex Training (aiming/target simulation)
│   ├── Game_1.c          Main game loop, input handling, rendering
│   └── Game_1.h          Public interface
├── game_2/               Turbo Overdrive (pseudo-3D racing)
│   ├── Game_2.c          Game logic, road rendering, collision
│   ├── Game_2.h          Public interface
│   ├── assets.c          Car sprites and obstacle graphics
│   ├── music.c           BGM and sound effects
│   └── ...assets
├── game_3/               Silk Knight: Abyss Patrol (platformer)
│   ├── Game_3.c          Game logic, player/enemy physics, rendering
│   ├── Game_3.h          Public interface
│   ├── g3_audio.c        Sound effects and buzzer control
│   └── ...game assets
├── shared/               Shared utilities across all games
│   ├── Menu.h/c          Multi-game menu system
│   ├── Joystick/         Analog joystick input abstraction
│   ├── Renderer/         LCD drawing primitives and line buffering
│   ├── Audio/            Buzzer driver with frequency control
│   └── ...common utilities
├── Drivers/              STM32 HAL and peripheral drivers
├── Core/                 Startup code and system initialization
└── ST7789V2_Driver/      LCD display controller driver
```

## Game Descriptions

### Game 1: Apex Training
**Gameplay**: First-person aiming challenge against scaling robot targets.

- **Input**: PS2 mouse for crosshair control, joystick for movement
- **Mechanics**:
  - Multiple difficulty levels with progressively challenging robot enemies
  - Three weapon classes: R99 carbine (high RoF), Heping shield (burst fire), Qiedao knife (melee)
  - Health/armor system with damage scaling
  - Pseudo-3D background with parallax layers from QSPI flash
- **Features**:
  - Real-time hit particle effects
  - Dynamic crosshair with aim correction
  - RGB LED feedback for status and level progression
  - Performance monitoring (INA219 power sensor)

### Game 2: Turbo Overdrive
**Gameplay**: Endless pseudo-3D racing with procedurally generated track.

- **Input**: Joystick for steering (left/right), speed auto-acceleration
- **Mechanics**:
  - Procedural road generation with smooth curves
  - Three distinct environments (country roads with vegetation, city streets)
  - Dynamic obstacles: traffic cars, yellow hazards, boost pickups
  - Time-based endurance: checkpoints grant time bonuses and health restoration
  - Multiple vehicle types with different handling characteristics
- **Features**:
  - Perspective-correct road rendering from lookup tables
  - Sprite-based scenery (houses, buildings) with depth culling
  - Collision detection using dilated sprite masks
  - Boost meter system with visual feedback
  - Best distance record saved to flash

### Game 3: Silk Knight: Abyss Patrol
**Gameplay**: Pixel-art platformer with knight character progression through three levels.

- **Input**: Joystick for movement (left/right), attack button trigger
- **Mechanics**:
  - Three knight classes: sword, lance, axe (each with different attack patterns)
  - Three difficulty levels with varying enemy density and damage scaling
  - Two-button combat: movement + one-button attack
  - Platform navigation with hazard tiles (spikes) and collectibles (healing)
  - Enemy AI with patrol patterns and collision response
  - Three-level progression with difficulty scaling
- **Features**:
  - Real-time platform collision detection (AABB)
  - Seven-segment display for level/knight selection
  - RGB LED health indicator bar
  - Dynamic tile-based level generation
  - Audio feedback for hits, heals, and level progression

## Technical Architecture

### Game Loop Pattern (All Games)

Each game follows a unified frame-based loop:

```
INPUT → UPDATE → RENDER
  ↓       ↓        ↓
Read     Game     Draw to
input    logic    screen
```

**Timing**: Target 60 FPS (16.67ms per frame) with frame-rate independent physics using delta-time.

### Input System

**Joystick** (analog stick with ADC):
- 8-directional or continuous angle measurement
- Magnitude 0-255 for sensitivity control
- Used by all games for primary character control

**PS2 Keyboard/Mouse** (Game 1 exclusive):
- PS2 clock/data bit-banging protocol
- Mouse streaming mode for Game 1 aiming
- ~60 Hz polling rate

### Rendering System

**LCD Driver** (ST7789V2):
- 240×240 RGB565 display
- Line-by-line rendering with DMA-accelerated SPI transfers
- Double-buffering in RAM for flicker-free animation

**Asset Storage** (QSPI Flash W25Q128):
- **Game 1**: Weapon animations and 3D backgrounds stored at 0x00300000 (12 MB partition)
- **Game 2**: Car sprites and obstacle graphics in program memory
- **Game 3**: Tileset and sprite data in program memory

### Audio System

**Buzzer** (PWM on TIM3):
- 20 kHz PWM carrier frequency
- Sound generation: 200-5000 Hz range
- Non-blocking duration-based beep system

**Music/SFX**:
- Game 1: Weapon feedback tones, impact sounds
- Game 2: Engine rumble, collision warnings, boost activation
- Game 3: Attack hits, enemy defeat, level clear fanfare

### Lighting/Feedback

**WS2812B RGB LED Strip** (10 LEDs):
- Game 1: Armor/health status bar, level progression colors
- Game 2: Speed meter (green→yellow→red), boost ready indicator
- Game 3: Health indicator with flash feedback on damage

## Building and Deployment

**Build System**: CMake
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Flash Memory**:
- STM32L476 internal flash (1 MB) for code
- W25Q128 QSPI flash (16 MB) for large assets (Game 1)

**Hardware Requirements**:
- STM32L476RG Nucleo board
- ST7789V2 LCD module (SPI interface)
- PS2 keyboard/mouse (Game 1)
- Analog joystick
- WS2812B RGB strip
- Optional: INA219 power monitor (Game 1)

## Game-Specific Controls

| Game | Input | Action |
|------|-------|--------|
| **Game 1** | Mouse | Aim crosshair |
| **Game 1** | Left Mouse | Fire weapon |
| **Game 1** | Joystick | Movement/dodge |
| **Game 2** | Joystick Left/Right | Steer |
| **Game 2** | Joystick Up | Accelerate (auto) |
| **Game 3** | Joystick Left/Right | Move |
| **Game 3** | Button (PC13) | Jump/Attack |

## Development Notes

### Adding a New Game

1. Create `game_N/Game_N.h` with entry point: `MenuState GameN_Run(void)`
2. Implement `MenuState GameN_Run(void)` in `game_N/Game_N.c`
3. Register in menu system (`shared/Menu.c`)
4. Return `MENU_STATE_HOME` to exit back to main menu

### Shared Utilities

- `shared/Menu.h`: Game selection and state machine
- `shared/Joystick/`: `Joystick_Read()` and input direction parsing
- `shared/Renderer/`: `LCD_FillBuffer()`, `LCD_DrawPixel()`, line rendering
- `shared/Audio/`: `Buzzer_Play(frequency, duration)`

### Performance Considerations

- **Game 1**: CPU-bound on rendering 3D background from QSPI at 60 FPS
- **Game 2**: Memory-constrained with procedural track generation; uses lookup tables
- **Game 3**: Tile-based collision detection scales with level size

## Hardware Connections

| STM32L476 Pin | Function | Device |
|---------------|----------|--------|
| PA4-PA6 | SPI LCD | ST7789V2 |
| PB7 | TIM4_CH2 PWM | WS2812B data |
| PA7 | SPI QSPI | W25Q128 |
| PA8, PB10-11 | I2C | INA219 (optional) |
| PA2, PA3 | ADC | Joystick X/Y |
| PA9-PA11 | PS2 | Keyboard/Mouse |
| PC13 | GPIO In | Action button |
| PB5, PB6 | GPIO | Seven-segment SEG G, F |

---

**Target Platform**: STM32L476RG @ 80 MHz, 96 KB RAM, 1 MB Flash  
**Display**: 240×240 RGB565 LCD @ 60 FPS  
**Language**: C (C99 with ARM Cortex-M4 extensions)
