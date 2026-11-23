#include "sanctum/NavigationController.hpp"
#include "sanctum/ViewOverlay.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

// Simple test counter: failed expectations print a message and are reported at the end.
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool close(float a, float b, float tolerance = 0.001F) {
    // Floating-point movement is compared with a small tolerance, not exact equality.
    return std::abs(a - b) <= tolerance;
}

// Fake renderer used to verify that the overlay requests the expected number of draws.
class RecordingCanvas final : public sanctum::OverlayCanvas {
public:
    void fillRect(sanctum::Rect, sanctum::Color) override { ++rectangles; }
    void drawText(std::string_view, float, float, sanctum::Color) override { ++texts; }
    float measureText(std::string_view text) const override {
        return static_cast<float>(text.size()) * 8.0F;
    }
    int rectangles{};
    int texts{};
};

void movementIsTimeBasedAndAvailableInFixedViews() {
    // Simulate the same one-second key hold using two different frame rates.
    sanctum::NavigationController slowFrames;
    sanctum::NavigationController fastFrames;
    slowFrames.selectView(sanctum::ViewMode::Top);
    fastFrames.selectView(sanctum::ViewMode::Top);
    slowFrames.onKey(sanctum::Key::W, true);
    fastFrames.onKey(sanctum::Key::W, true);

    for (int i = 0; i < 10; ++i) {
        slowFrames.update(0.1F);
    }
    for (int i = 0; i < 100; ++i) {
        fastFrames.update(0.01F);
    }

    expect(close(slowFrames.playerPosition().z, fastFrames.playerPosition().z),
           "movement should be independent of frame rate");
    // A fixed camera does not disable player movement.
    expect(slowFrames.playerPosition().z < 0.0F,
           "W movement should work while a fixed camera view is selected");
}

void sceneChangesClampAndAllowDirectSelection() {
    // Hold D long enough to reach the Library's right edge.
    sanctum::NavigationController navigation;
    navigation.onKey(sanctum::Key::D, true);
    navigation.update(10.0F);
    navigation.onKey(sanctum::Key::D, false);
    expect(close(navigation.playerPosition().x, 9.0F), "player should stop at the library edge");

    // Relic Room is narrower, so switching there clamps the player to its boundary.
    navigation.selectScene(sanctum::SceneId::RelicRoom);
    expect(close(navigation.playerPosition().x, 8.0F),
           "switching scenes should clamp the player to the relic room boundary");
    navigation.selectScene(sanctum::SceneId::Library);
    expect(navigation.scene() == sanctum::SceneId::Library,
           "direct scene selection should work in any order");
    navigation.toggleScene();
    expect(navigation.scene() == sanctum::SceneId::RelicRoom, "scene toggle should work");
}

void perspectiveToggleAndNavigationResetWork() {
    // Perspective starts third-person, and pressing the same view toggles to first-person.
    sanctum::NavigationController navigation;
    expect(navigation.perspectiveStyle() == sanctum::PerspectiveStyle::ThirdPerson,
           "third-person perspective should be the initial navigation view");
    navigation.onKey(sanctum::Key::Perspective, true);
    expect(navigation.perspectiveStyle() == sanctum::PerspectiveStyle::FirstPerson,
           "Perspective should switch to first person when already selected");
    navigation.onKey(sanctum::Key::Perspective, false);
    navigation.onKey(sanctum::Key::Perspective, true);
    expect(navigation.perspectiveStyle() == sanctum::PerspectiveStyle::ThirdPerson,
           "Perspective should switch back to third person");

    // A scripted interaction can temporarily supply its own camera frame.
    sanctum::CameraFrame cut;
    cut.eye = {10.0F, 10.0F, 10.0F};
    navigation.setCameraOverride(cut);
    expect(close(navigation.camera(1.0F).eye.x, 10.0F),
           "camera override should be available to interaction code");
    navigation.returnToNavigation();
    expect(navigation.view() == sanctum::ViewMode::Perspective &&
               navigation.perspectiveStyle() == sanctum::PerspectiveStyle::ThirdPerson,
           "navigation reset should restore normal third-person controls");
    expect(!close(navigation.camera(1.0F).eye.x, 10.0F),
           "navigation reset should clear the camera override");
}

void fixedViewsFrameTheirSceneAndOverlayButtonsCanBeClicked() {
    // Fixed views use orthographic camera framing for the complete room.
    sanctum::NavigationController navigation;
    navigation.selectView(sanctum::ViewMode::Top);
    const auto top = navigation.camera(16.0F / 9.0F);
    expect(top.orthographic, "top view should use orthographic projection");
    expect(top.orthographicHalfHeight * 2.0F >= 22.0F,
           "top view should frame the full library depth");

    // The overlay shares its button rectangles between drawing and click detection.
    sanctum::ViewOverlay overlay;
    const auto buttons = overlay.buttons(800.0F);
    const auto clicked = overlay.viewAt(buttons[3].bounds.x + 2.0F,
                                        buttons[3].bounds.y + 2.0F, 800.0F);
    expect(clicked == sanctum::ViewMode::Perspective,
           "clicking the Perspective overlay button should select that view");
    expect(overlay.handleClick(navigation, buttons[3].bounds.x + 2.0F,
                               buttons[3].bounds.y + 2.0F, 800.0F),
           "clicking a view button should activate it");
    expect(navigation.perspectiveStyle() == sanctum::PerspectiveStyle::ThirdPerson,
           "entering Perspective from a fixed view should use third-person framing");
    expect(overlay.handleClick(navigation, buttons[3].bounds.x + 2.0F,
                               buttons[3].bounds.y + 2.0F, 800.0F),
           "clicking Perspective again should remain an accepted button action");
    expect(navigation.perspectiveStyle() == sanctum::PerspectiveStyle::FirstPerson,
           "activating Perspective again should toggle first-person framing");

    expect(!overlay.handleClick(navigation, 0.0F, 0.0F, 800.0F),
           "clicking outside the controls should not change the view");

    RecordingCanvas canvas;
    overlay.draw(canvas, 800.0F, sanctum::ViewMode::Perspective);
    expect(canvas.rectangles == 4 && canvas.texts == 4,
           "overlay should draw four 2D buttons and their labels");
}

} // namespace

int main() {
    // Keep tests small and independent; report every failure in one run.
    movementIsTimeBasedAndAvailableInFixedViews();
    sceneChangesClampAndAllowDirectSelection();
    perspectiveToggleAndNavigationResetWork();
    fixedViewsFrameTheirSceneAndOverlayButtonsCanBeClicked();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All navigation tests passed.\n";
    return EXIT_SUCCESS;
}
