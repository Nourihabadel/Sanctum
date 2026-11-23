#include "sanctum/ViewOverlay.hpp"

#include <algorithm>

namespace sanctum {
namespace {

// Purple toolbar styling and spacing, measured in logical window pixels.
constexpr float margin = 16.0F;
constexpr float gap = 8.0F;
constexpr float buttonHeight = 36.0F;
constexpr float labelPadding = 24.0F;
constexpr float labelTopOffset = 11.0F;
constexpr Color inactiveColor{0.10F, 0.07F, 0.16F, 0.92F};
constexpr Color activeColor{0.39F, 0.22F, 0.62F, 0.98F};
constexpr Color labelColor{0.96F, 0.92F, 1.0F, 1.0F};

constexpr std::array<ViewMode, 4> views{
    ViewMode::Top, ViewMode::Front, ViewMode::Side, ViewMode::Perspective
};

constexpr std::array<std::string_view, 4> labels{
    "Top", "Front", "Side", "Perspective"
};

} // namespace

std::array<ViewButton, 4> ViewOverlay::buttons(float windowWidth) const {
    std::array<ViewButton, 4> result{};
    // Leave margins at both edges and shrink buttons only when the window is too narrow.
    const float availableWidth = std::max(0.0F, windowWidth - 2.0F * margin);
    const float totalGaps = gap * static_cast<float>(result.size() - 1);
    const float desiredWidths[] = {64.0F, 76.0F, 64.0F, 112.0F};
    float desiredTotal = totalGaps;
    for (float width : desiredWidths) {
        desiredTotal += width;
    }
    const float shrink = desiredTotal > totalGaps
                             ? std::min(1.0F, (availableWidth - totalGaps) /
                                                   (desiredTotal - totalGaps))
                             : 1.0F;

    float x = margin;
    for (std::size_t i = 0; i < result.size(); ++i) {
        // Every button and click target is generated from this same rectangle.
        const float width = std::max(0.0F, desiredWidths[i] * shrink);
        result[i] = {views[i], labels[i], {x, margin, width, buttonHeight}};
        x += width + gap;
    }
    return result;
}

void ViewOverlay::draw(OverlayCanvas& canvas, float windowWidth, ViewMode activeView) const {
    // The active view gets a brighter background, and each label is centered.
    for (const ViewButton& button : buttons(windowWidth)) {
        canvas.fillRect(button.bounds,
                        button.view == activeView ? activeColor : inactiveColor);
        const float textX = button.bounds.x +
                            (button.bounds.width - canvas.measureText(button.label)) * 0.5F;
        canvas.drawText(button.label, textX, button.bounds.y + labelTopOffset, labelColor);
    }
}

std::optional<ViewMode> ViewOverlay::viewAt(float x, float y, float windowWidth) const {
    // No returned value means the pointer was outside all view buttons.
    for (const ViewButton& button : buttons(windowWidth)) {
        const Rect& bounds = button.bounds;
        if (x >= bounds.x && x <= bounds.x + bounds.width &&
            y >= bounds.y && y <= bounds.y + bounds.height) {
            return button.view;
        }
    }
    return std::nullopt;
}

bool ViewOverlay::handleClick(NavigationController& navigation,
                              float x, float y, float windowWidth) const {
    // Share hit-testing with viewAt, then tell the controller to activate the result.
    const std::optional<ViewMode> clickedView = viewAt(x, y, windowWidth);
    if (!clickedView) {
        return false;
    }
    navigation.activateView(*clickedView);
    return true;
}

} // namespace sanctum
