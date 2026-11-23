#include "sanctum/NavigationController.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sanctum {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr float radiansToDegrees = 180.0F / pi;
constexpr float maxPitchRadians = 1.483529864F; // 85 degrees, just short of looking straight up.

// Room dimensions are world units and should match the geometry in the renderer.
constexpr SceneLayout libraryLayout{
    {0.0F, 0.0F, 0.0F},
    -9.0F, 9.0F,
    -11.0F, 11.0F,
    6.0F
};

constexpr SceneLayout relicRoomLayout{
    {0.0F, 0.0F, 0.0F},
    -8.0F, 8.0F,
    -9.0F, 9.0F,
    6.0F
};

float clampFiniteAspect(float aspectRatio) {
    // A minimized or not-yet-sized window can briefly report an invalid aspect ratio.
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0F) {
        return 1.0F;
    }
    return aspectRatio;
}

Vec3 add(Vec3 a, Vec3 b) {
    // Tiny vector helpers keep this module independent of GLM.
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scale(Vec3 value, float amount) {
    return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 forwardFor(float yaw, float pitch) {
    // At yaw=0 and pitch=0, forward points along negative Z.
    // Yaw turns left/right; pitch tilts the viewing direction up/down.
    const float horizontal = std::cos(pitch);
    return {
        -std::sin(yaw) * horizontal,
        std::sin(pitch),
        -std::cos(yaw) * horizontal
    };
}

CameraFrame orthographicCamera(Vec3 eye, Vec3 target, Vec3 up,
                               float horizontalExtent, float verticalExtent,
                               float aspectRatio) {
    // Fit the requested room extents to the viewport, with a little breathing room.
    constexpr float framingMargin = 1.12F;
    const float aspect = clampFiniteAspect(aspectRatio);
    const float halfHeight = std::max(verticalExtent * 0.5F,
                                      horizontalExtent * 0.5F / aspect) * framingMargin;
    const float halfWidth = halfHeight * aspect;

    CameraFrame result;
    result.eye = eye;
    result.target = target;
    result.up = up;
    result.orthographic = true;
    result.orthographicHalfWidth = halfWidth;
    result.orthographicHalfHeight = halfHeight;
    return result;
}

} // namespace

NavigationController::NavigationController(NavigationConfig config)
    : config_(config), playerPosition_(libraryLayout.center) {
    // Invalid speeds would make movement unpredictable, so reject them up front.
    if (!std::isfinite(config_.moveSpeed) || config_.moveSpeed < 0.0F ||
        !std::isfinite(config_.turnSpeedRadiansPerSecond) ||
        config_.turnSpeedRadiansPerSecond < 0.0F ||
        !std::isfinite(config_.pitchSpeedRadiansPerSecond) ||
        config_.pitchSpeedRadiansPerSecond < 0.0F) {
        throw std::invalid_argument("Navigation speeds must be finite and non-negative.");
    }
}

void NavigationController::onKey(Key key, bool isDown) {
    // Count is an array-size marker, not a physical key.
    if (key == Key::Count) {
        return;
    }

    // One-shot actions run only on a fresh press, not repeatedly while held.
    const bool wasDown = keyDown(key);
    setKeyDown(key, isDown);
    if (!isDown || wasDown) {
        return;
    }

    switch (key) {
    // The app maps actual keyboard keys to these abstract navigation commands.
    case Key::Top:
        activateView(ViewMode::Top);
        break;
    case Key::Front:
        activateView(ViewMode::Front);
        break;
    case Key::Side:
        activateView(ViewMode::Side);
        break;
    case Key::Perspective:
        activateView(ViewMode::Perspective);
        break;
    case Key::ToggleScene:
        toggleScene();
        break;
    case Key::Library:
        selectScene(SceneId::Library);
        break;
    case Key::RelicRoom:
        selectScene(SceneId::RelicRoom);
        break;
    case Key::ReturnToNavigation:
        returnToNavigation();
        break;
    default:
        break;
    }
}

void NavigationController::update(float deltaSeconds) {
    // The caller supplies elapsed seconds since the previous frame.
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return;
    }

    if (view_ == ViewMode::Perspective && !cameraOverride_) {
        // Arrows rotate the player-following camera only in Perspective mode.
        const float turnInput = static_cast<float>(keyDown(Key::ArrowRight)) -
                                static_cast<float>(keyDown(Key::ArrowLeft));
        const float pitchInput = static_cast<float>(keyDown(Key::ArrowUp)) -
                                 static_cast<float>(keyDown(Key::ArrowDown));
        playerYawRadians_ += turnInput * config_.turnSpeedRadiansPerSecond * deltaSeconds;
        playerYawRadians_ = std::remainder(playerYawRadians_, 2.0F * pi);
        viewPitchRadians_ = std::clamp(
            viewPitchRadians_ + pitchInput * config_.pitchSpeedRadiansPerSecond * deltaSeconds,
            -maxPitchRadians, maxPitchRadians);
    }

    if (cameraOverride_) {
        // An interaction close-up owns the camera and temporarily locks navigation.
        return;
    }

    // Convert held movement keys into signed forward/back and right/left inputs.
    const float forwardInput = static_cast<float>(keyDown(Key::W)) -
                               static_cast<float>(keyDown(Key::S));
    const float rightInput = static_cast<float>(keyDown(Key::D)) -
                             static_cast<float>(keyDown(Key::A));
    const float magnitude = std::sqrt(forwardInput * forwardInput + rightInput * rightInput);
    if (magnitude == 0.0F) {
        return;
    }

    // Project movement onto the player's forward and right directions.
    const float forwardX = -std::sin(playerYawRadians_);
    const float forwardZ = -std::cos(playerYawRadians_);
    const float rightX = std::cos(playerYawRadians_);
    const float rightZ = -std::sin(playerYawRadians_);
    // Normalize diagonals so holding W and D is not faster than holding only W.
    const float distance = config_.moveSpeed * deltaSeconds / std::max(1.0F, magnitude);

    playerPosition_.x += (forwardX * forwardInput + rightX * rightInput) * distance;
    playerPosition_.z += (forwardZ * forwardInput + rightZ * rightInput) * distance;
    clampPlayerToScene();
}

void NavigationController::selectView(ViewMode view) {
    // Force a view and clear any camera cut that might otherwise override it.
    view_ = view;
    cameraOverride_.reset();
}

void NavigationController::activateView(ViewMode view) {
    // Pressing Perspective again toggles first-/third-person without leaving that mode.
    if (view == ViewMode::Perspective && view_ == ViewMode::Perspective) {
        switchPerspectiveStyle();
        cameraOverride_.reset();
        return;
    }
    selectView(view);
}

void NavigationController::selectScene(SceneId scene) {
    // Room bounds differ, so clamp the current player position after a scene change.
    scene_ = scene;
    clampPlayerToScene();
}

void NavigationController::toggleScene() {
    // This convenience shortcut alternates rooms; direct selection is also available.
    selectScene(scene_ == SceneId::Library ? SceneId::RelicRoom : SceneId::Library);
}

void NavigationController::setCameraOverride(std::optional<CameraFrame> camera) {
    // Supplying a frame enables a cutaway; nullopt removes the temporary camera.
    cameraOverride_ = camera;
}

void NavigationController::returnToNavigation() {
    // Restore the standard camera, but deliberately keep player position and direction.
    cameraOverride_.reset();
    view_ = ViewMode::Perspective;
    perspectiveStyle_ = PerspectiveStyle::ThirdPerson;
}

const SceneLayout& NavigationController::sceneLayout() const {
    return layoutFor(scene_);
}

const SceneLayout& NavigationController::layoutFor(SceneId scene) {
    // One layout lookup is shared by camera framing and edge clamping.
    switch (scene) {
    case SceneId::Library:
        return libraryLayout;
    case SceneId::RelicRoom:
        return relicRoomLayout;
    }
    return libraryLayout;
}

CameraFrame NavigationController::camera(float viewportAspectRatio) const {
    // Scripted camera takes priority; otherwise choose player-following or fixed framing.
    if (cameraOverride_) {
        return *cameraOverride_;
    }
    if (view_ == ViewMode::Perspective) {
        return perspectiveCamera();
    }
    return fixedCamera(viewportAspectRatio);
}

bool NavigationController::keyDown(Key key) const {
    // Check the sentinel before converting an enum value into an array index.
    return key != Key::Count && keysDown_[static_cast<std::size_t>(key)];
}

void NavigationController::setKeyDown(Key key, bool isDown) {
    if (key != Key::Count) {
        keysDown_[static_cast<std::size_t>(key)] = isDown;
    }
}

void NavigationController::clampPlayerToScene() {
    // Keep the player on the floor and clamp horizontal coordinates to this room.
    const SceneLayout& layout = sceneLayout();
    playerPosition_.x = std::clamp(playerPosition_.x, layout.minX, layout.maxX);
    playerPosition_.z = std::clamp(playerPosition_.z, layout.minZ, layout.maxZ);
    playerPosition_.y = layout.center.y;
}

void NavigationController::switchPerspectiveStyle() {
    // Perspective style is remembered separately from the active view mode.
    perspectiveStyle_ = perspectiveStyle_ == PerspectiveStyle::ThirdPerson
                            ? PerspectiveStyle::FirstPerson
                            : PerspectiveStyle::ThirdPerson;
}

CameraFrame NavigationController::fixedCamera(float aspectRatio) const {
    const SceneLayout& layout = sceneLayout();
    const float width = layout.maxX - layout.minX;
    const float depth = layout.maxZ - layout.minZ;
    const Vec3 center{layout.center.x, layout.center.y + layout.height * 0.5F,
                      layout.center.z};

    switch (view_) {
    case ViewMode::Top:
        // Looking down from above; this up vector keeps room depth oriented on screen.
        return orthographicCamera(
            {layout.center.x, layout.center.y + 28.0F, layout.center.z},
            {layout.center.x, layout.center.y, layout.center.z},
            {0.0F, 0.0F, -1.0F}, width, depth, aspectRatio);
    case ViewMode::Front:
        // Stand outside the positive-Z wall and aim at the room center.
        return orthographicCamera(
            {layout.center.x, center.y, layout.maxZ + 24.0F},
            center, {0.0F, 1.0F, 0.0F}, width, layout.height, aspectRatio);
    case ViewMode::Side:
        // Stand outside the positive-X wall and aim at the room center.
        return orthographicCamera(
            {layout.maxX + 24.0F, center.y, layout.center.z},
            center, {0.0F, 1.0F, 0.0F}, depth, layout.height, aspectRatio);
    case ViewMode::Perspective:
        break;
    }

    return perspectiveCamera();
}

CameraFrame NavigationController::perspectiveCamera() const {
    // Camera direction shares player yaw, so turning also changes movement direction.
    const Vec3 horizontalForward = forwardFor(playerYawRadians_, 0.0F);
    const Vec3 lookDirection = forwardFor(playerYawRadians_, viewPitchRadians_);
    const Vec3 eyeHeight{0.0F, 1.65F, 0.0F};
    CameraFrame result;
    result.orthographic = false;
    result.verticalFieldOfViewDegrees = 60.0F;

    if (perspectiveStyle_ == PerspectiveStyle::FirstPerson) {
        // Eye-height camera; the target is ahead along the current yaw/pitch direction.
        result.eye = add(playerPosition_, eyeHeight);
        result.target = add(result.eye, scale(lookDirection, 8.0F));
    } else {
        // Third-person camera trails behind and above the player, offset over one shoulder.
        const Vec3 behind = scale(horizontalForward, -3.2F);
        const Vec3 shoulder{0.45F, 2.25F, 0.0F};
        result.eye = add(add(playerPosition_, behind), shoulder);
        result.target = add(add(playerPosition_, {0.0F, 1.25F, 0.0F}),
                            scale(lookDirection, 8.0F));
    }
    return result;
}

} // namespace sanctum
