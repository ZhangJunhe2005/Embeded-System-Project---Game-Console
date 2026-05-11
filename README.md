# Pong Game Engine

A complete implementation of the classic Pong game demonstrating core game development concepts: object-oriented design, collision detection, and the standard game loop pattern.

## Overview

This project implements Pong with a clean architecture:
- **Ball**: Moves in straight lines with velocity, bounces off walls and paddle
- **Paddle**: Controlled by joystick up/down, constrained to screen bounds
- **Collisions**: AABB (Axis-Aligned Bounding Box) collision detection between ball and paddle
- **Game State**: Lives system (start with 4, game over at 0) and score tracking
- **Audio**: Buzzer sounds on collisions (wall: 1200 Hz, paddle: 800 Hz)

## Code Organization

```
PongEngine/
├── PongEngine.h/c       Main game engine (collisions, game state)
├── Ball.h/c              Ball object (position, velocity)
├── Paddle.h/c            Paddle object (joystick input)
Core/Inc/
└── Utils.h               AABB collision detection, shared types
Core/Src/
└── main.c                Game initialization and main loop
```

## The Game Loop

Every frame (at 60 FPS), the game follows a simple pattern:

```
INPUT → UPDATE → RENDER
  ↓       ↓        ↓
 Read     Game     Draw to
 joy      logic    screen
 stick
```

### Main Loop in main.c

```c
uint32_t last_tick = HAL_GetTick();

while (!game_over) {
    // Frame timing: aim for 60 FPS (~16.67ms per frame)
    uint32_t now = HAL_GetTick();
    if ((now - last_tick) < FRAME_TIME_MS) {
        continue;  // Skip if not enough time has passed
    }
    last_tick = now;

    // STEP 1: INPUT - Read joystick
    Joystick_Read(&joystick_cfg, &joystick_data);
    UserInput input = Joystick_GetInput(&joystick_data);

    // STEP 2: UPDATE - Game logic
    update_pong(input);

    // STEP 3: RENDER - Draw to screen
    render_pong();
}
```

### What Happens in Each Step

#### Step 1: INPUT (Joystick Reading)

```c
Joystick_Read(&joystick_cfg, &joystick_data);
UserInput input = Joystick_GetInput(&joystick_data);
```

- Reads X and Y values from joystick ADC
- Converts raw ADC values to direction and magnitude
- Returns a `UserInput` struct with:
  - `direction`: N, S, E, W, NE, SE, NW, SW, or CENTER
  - `magnitude`: strength of input (0 to 255)

#### Step 2: UPDATE (Game Logic)

```c
void update_pong(UserInput input) {
    uint8_t lives = PongEngine_Update(&pong_engine, input);
    if (lives == 0) {
        game_over = 1;
    }
}
```

This calls `PongEngine_Update()`, which does:

1. **Paddle Update** - Move paddle based on joystick input (N/S only)
   - File: [Paddle/Paddle.c](Paddle/Paddle.c#L26)
   - Constraints: Paddle stays within screen bounds (y = 0 to 200)

2. **Ball Update** - Move ball based on velocity
   - File: [Ball/Ball.c](Ball/Ball.c#L18)
   - Simple: `x += velocity.x`, `y += velocity.y`

3. **Wall Collision Check** - Bounce ball off top/bottom/right walls
   - File: [PongEngine/PongEngine.c](PongEngine/PongEngine.c#L63)
   - Simple boundary checks: `if (ball->y <= 0) reverse Y velocity`

4. **Paddle Collision Check** - Use AABB to detect ball-paddle overlap
   - File: [PongEngine/PongEngine.c](PongEngine/PongEngine.c#L101)
   - Only collision test using `AABB_Collides()`
   - If collision: reverse ball X velocity, play sound (800 Hz), increment score

5. **Goal Check** - Did ball leave the play area?
   - File: [PongEngine/PongEngine.c](PongEngine/PongEngine.c#L129)
   - If `ball->x < 0`: decrement lives, reset ball to center

6. **Buzzer Update** - Stop beep sound (non-blocking decay)
   - Allows beep to stop automatically without blocking main loop

#### Step 3: RENDER (Drawing)

```c
void render_pong(void) {
    LCD_Fill_Buffer(0);          // 1. Clear screen
    PongEngine_Draw(&pong_engine);          // 2. Draw objects
    // Display lives and score as text
}
```

This happens once per frame and includes:
1. Clear entire LCD buffer
2. Draw ball sprite (6x6 pixels)
3. Draw paddle sprite (4x40 pixels)
4. Draw lives and score as text

---

## Suggested Student Activities

### Activity 1: Change Ball Speed and Size
Find where the ball is initialized in `main.c` and change the speed and size parameters to `PongEngine_Init()`.

### Activity 2: Make the Paddle Smaller
Find where the paddle height is set in `main.c` and reduce it. Rebuild and test.

### Activity 3: Add Sound When a Life is Lost
Find `PongEngine_CheckGoal()` in `PongEngine.c` where `lives` is decremented. Call `PongEngine_Beep()` with a low frequency (try 400 Hz).

### Activity 4: Add a Game Over Sound
After the main game loop ends in `main.c`, play a buzzer sound before displaying the game over screen. Try frequencies between 200-500 Hz for effect.

### Activity 5: Speed Up Ball After Every 10 Points
Track the score in `PongEngine.c`. When the score increases by 10, increase the ball velocity by 10% and play a high-pitched beep (try 2000 Hz). Hint: in `PongEngine_Update()`, you can use `Paddle_GetScore()` to check the current score.
