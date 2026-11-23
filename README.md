# Sanctum navigation module

This clean project implements Person 1's navigation and view-control portion for
the Library and Relic Room. The controller is independent of GLFW/GLM so it can
be unit-tested now and connected to those libraries by the application layer.

On Windows, it also includes a small runnable demo window. The demo uses simple
drawn room furniture and a placeholder sorcerer to make navigation visible; it
is not a replacement for the final 3D models or renderer.

## Controls

| Input | Action |
| --- | --- |
| W / A / S / D | Move the player (held movement is elapsed-time based) |
| Arrow keys | Turn/look in Perspective mode |
| F1 / F2 / F3 / F4 | Top / Front / Side / Perspective |
| F4 or Perspective button | Enter Perspective; activate again to toggle first-/third-person |
| Tab | Switch between Library and Relic Room |
| 1 / 2 or room buttons | Select Library / Relic Room |
| N | Return to normal third-person navigation; Esc closes the demo |

Fixed cameras use the active scene's bounds and frame its full width/depth or
width/height. The player remains movable in all four views. Perspective is
player-following; first person places the camera at eye height and third person
places it behind the player's shoulder.

## Hooking up the application

1. Translate GLFW key press/release events to `sanctum::Key` and call
   `NavigationController::onKey(key, isDown)`. For example, map
   `GLFW_KEY_F1..GLFW_KEY_F4` to `Top`, `Front`, `Side`, and `Perspective`;
   map `GLFW_KEY_1`, `GLFW_KEY_2`, and `GLFW_KEY_TAB` to the scene-selection
   keys. Call `update(deltaSeconds)` once per frame, using elapsed seconds.
2. Use `playerPosition()` and `playerYawRadians()` for the player transform.
   The renderer builds its view/projection matrices from `camera(aspectRatio)`.
3. Implement `OverlayCanvas` with the renderer's 2D drawing methods. Call
   `ViewOverlay::draw(canvas, windowWidth, navigation.view())` after the 3D
   scene. On a pointer click, call `ViewOverlay::handleClick(navigation, x, y,
   windowWidth)` to hit-test and activate a view.
4. An interaction can temporarily call `setCameraOverride(cameraFrame)`;
   call `returnToNavigation()` when it is finished.

The UI expects logical-pixel coordinates with a top-left origin. Convert GLFW's
bottom-left cursor Y coordinate before calling `viewAt`.

## Build and test

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To open the interactive Windows demo after building:

```powershell
.\build\sanctum_navigation_demo.exe
```
