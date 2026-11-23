#pragma once

#include "sanctum/NavigationController.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace sanctum {

struct Rect {
    // Positions and sizes are logical window pixels, with (0,0) at top-left.
    float x{};
    float y{};
    float width{};
    float height{};
};

struct Color {
    // Renderer-independent normalized color channels (each usually between 0 and 1).
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

struct ViewButton {
    ViewMode view{};
    std::string_view label;
    Rect bounds;
};

// Rendering adapter: navigation chooses the layout; the application supplies drawing.
class OverlayCanvas {
public:
    virtual ~OverlayCanvas() = default;
    // Paint a button background and draw its label using the requested color.
    virtual void fillRect(Rect bounds, Color color) = 0;
    virtual void drawText(std::string_view text, float x, float y, Color color) = 0;
    // Lets the overlay center a label without making assumptions about font metrics.
    [[nodiscard]] virtual float measureText(std::string_view text) const = 0;
};

class ViewOverlay {
public:
    // Compute a shared set of rectangles used by both rendering and hit-testing.
    [[nodiscard]] std::array<ViewButton, 4> buttons(float windowWidth) const;

    // Draws four labeled controls; the active view is visually highlighted.
    void draw(OverlayCanvas& canvas, float windowWidth, ViewMode activeView) const;

    // Returns a view only when a click falls inside one of the four button rectangles.
    [[nodiscard]] std::optional<ViewMode> viewAt(float x, float y,
                                                 float windowWidth) const;

    // Hit-tests a click and activates the matching view, including the Perspective toggle.
    [[nodiscard]] bool handleClick(NavigationController& navigation,
                                   float x, float y, float windowWidth) const;
};

} // namespace sanctum
