#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace sanctum {

// Small world-space vector used here to avoid a dependency on a separate math library.
struct Vec3 {
    float x{};
    float y{};
    float z{};
};

// The active room controls which scene is shown and where the player can move.
enum class SceneId {
    Library,
    RelicRoom
};

// Perspective is one view mode; first-/third-person framing is a separate toggle.
enum class ViewMode {
    Top,
    Front,
    Side,
    Perspective
};

enum class PerspectiveStyle {
    FirstPerson,
    ThirdPerson
};

// These abstract keys keep this code independent of GLFW/Win32 key codes.
// The application translates real keyboard events into these values.
enum class Key {
    W,
    A,
    S,
    D,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    Top,
    Front,
    Side,
    Perspective,
    ToggleScene,
    Library,
    RelicRoom,
    ReturnToNavigation,
    Count
};

struct SceneLayout {
    Vec3 center; // World-space point the fixed cameras look toward.
    float minX{}; // Left boundary the player is allowed to reach.
    float maxX{}; // Right boundary the player is allowed to reach.
    float minZ{}; // Back boundary in world space.
    float maxZ{}; // Front boundary in world space.
    float height{}; // Approximate wall height, used to aim fixed cameras.
};

struct CameraFrame {
    Vec3 eye; // Camera position in world space.
    Vec3 target; // Point the camera looks toward.
    Vec3 up{0.0F, 1.0F, 0.0F}; // Establishes which direction appears vertical on screen.
    bool orthographic{}; // Fixed views use orthographic; player views use perspective.
    float verticalFieldOfViewDegrees{60.0F}; // Used by perspective projection.
    float orthographicHalfWidth{}; // Half of the visible horizontal span for fixed views.
    float orthographicHalfHeight{}; // Half of the visible vertical span for fixed views.
};

struct NavigationConfig {
    float moveSpeed{3.0F}; // World units traveled per second while moving.
    float turnSpeedRadiansPerSecond{1.8F}; // Horizontal look speed.
    float pitchSpeedRadiansPerSecond{1.4F}; // Vertical look speed.
};

class NavigationController {
public:
    // Starts in the Library, centered in the room, using third-person Perspective.
    explicit NavigationController(NavigationConfig config = {});

    // Call once for each key press/release from the window system. Repeated key-down
    // events are ignored, so one-shot commands (like changing scenes) don't repeat.
    void onKey(Key key, bool isDown);

    // Advance movement and view rotation by elapsed seconds (not by frame count).
    void update(float deltaSeconds);

    // Force a view without toggling Perspective's first-/third-person style.
    void selectView(ViewMode view);
    // Activating Perspective repeatedly toggles first-/third-person framing.
    void activateView(ViewMode view);

    // Scene selection works in any order; switching clamps the player into the new room.
    void selectScene(SceneId scene);
    void toggleScene();

    // External camera cuts (for example, an interaction) can temporarily override
    // navigation. Calling this with nullopt or returnToNavigation restores player control.
    void setCameraOverride(std::optional<CameraFrame> camera);
    void returnToNavigation();

    // Read-only state getters are used by the renderer and HUD.
    [[nodiscard]] SceneId scene() const { return scene_; }
    [[nodiscard]] ViewMode view() const { return view_; }
    [[nodiscard]] PerspectiveStyle perspectiveStyle() const { return perspectiveStyle_; }
    [[nodiscard]] Vec3 playerPosition() const { return playerPosition_; }
    [[nodiscard]] float playerYawRadians() const { return playerYawRadians_; }
    [[nodiscard]] const SceneLayout& sceneLayout() const;
    [[nodiscard]] CameraFrame camera(float viewportAspectRatio) const;

    [[nodiscard]] static const SceneLayout& layoutFor(SceneId scene);

private:
    // Count is an array-size marker, not a key that can be pressed.
    static constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);

    [[nodiscard]] bool keyDown(Key key) const;
    void setKeyDown(Key key, bool isDown);
    void clampPlayerToScene();
    void switchPerspectiveStyle();
    [[nodiscard]] CameraFrame fixedCamera(float aspectRatio) const;
    [[nodiscard]] CameraFrame perspectiveCamera() const;

    NavigationConfig config_; // Speeds are configurable without changing movement code.
    SceneId scene_{SceneId::Library};
    ViewMode view_{ViewMode::Perspective};
    PerspectiveStyle perspectiveStyle_{PerspectiveStyle::ThirdPerson};
    Vec3 playerPosition_{}; // Player's ground position; y is set from the room center.
    float playerYawRadians_{}; // Horizontal facing direction, in radians.
    float viewPitchRadians_{}; // Vertical look angle, in radians.
    std::array<bool, keyCount> keysDown_{}; // Held-key state makes movement continuous.
    std::optional<CameraFrame> cameraOverride_; // Temporary close-up or scripted camera.
};

} // namespace sanctum
