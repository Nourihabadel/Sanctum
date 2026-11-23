#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "sanctum/NavigationController.hpp"
#include "sanctum/ViewOverlay.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Short aliases keep the Win32 drawing code readable.
using sanctum::CameraFrame;
using sanctum::Color;
using sanctum::NavigationController;
using sanctum::OverlayCanvas;
using sanctum::Rect;
using sanctum::SceneId;
using sanctum::Vec3;
using sanctum::ViewMode;
using sanctum::ViewOverlay;

constexpr wchar_t windowClassName[] = L"SanctumNavigationDemo";
constexpr COLORREF backgroundColor = RGB(13, 11, 22);
constexpr float pi = 3.14159265358979323846F;

// Lightweight 3D point used by this demo's hand-written projection and drawing helpers.
struct Point3 {
    float x;
    float y;
    float z;
};

struct ScreenPoint {
    int x;
    int y;
    float depth;
    bool visible;
};

struct DrawFace {
    std::array<Point3, 4> corners;
    COLORREF color;
};

struct Button {
    Rect bounds;
    std::wstring label;
    bool selected;
};

// The window and the navigation module share one controller instance.
NavigationController navigation;
ViewOverlay viewOverlay;
std::array<bool, 4> movementKeys{}; // W, A, S, D states, in that order.
std::chrono::steady_clock::time_point previousFrame; // Used for frame-rate-independent updates.

float dot(Point3 a, Point3 b) {
    // Dot products measure how much a point lies along a camera axis.
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Point3 subtract(Point3 a, Point3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Point3 cross(Point3 a, Point3 b) {
    // Cross products create perpendicular camera axes from the look and up directions.
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Point3 normalized(Point3 value) {
    // Camera direction vectors must be unit length for projection distances to be meaningful.
    const float length = std::sqrt(dot(value, value));
    if (length < 0.0001F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {value.x / length, value.y / length, value.z / length};
}

Point3 asPoint(Vec3 value) {
    // Convert the controller's vector type to this file's projection helper type.
    return {value.x, value.y, value.z};
}

COLORREF darker(COLORREF color, float amount) {
    // Darken a face color to make the sample box geometry look shaded.
    return RGB(static_cast<BYTE>(GetRValue(color) * amount),
               static_cast<BYTE>(GetGValue(color) * amount),
               static_cast<BYTE>(GetBValue(color) * amount));
}

class GdiCanvas final : public OverlayCanvas {
public:
    // Adapt the generic overlay drawing interface to Win32 GDI.
    explicit GdiCanvas(HDC dc) : dc_(dc) {}

    void fillRect(Rect bounds, Color color) override {
        // Fill the view button, then add a thin outline for contrast.
        RECT rect{
            static_cast<LONG>(bounds.x),
            static_cast<LONG>(bounds.y),
            static_cast<LONG>(bounds.x + bounds.width),
            static_cast<LONG>(bounds.y + bounds.height)
        };
        const COLORREF fill = RGB(static_cast<BYTE>(color.red * 255.0F),
                                  static_cast<BYTE>(color.green * 255.0F),
                                  static_cast<BYTE>(color.blue * 255.0F));
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(dc_, &rect, brush);
        DeleteObject(brush);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(118, 86, 153));
        HGDIOBJ oldPen = SelectObject(dc_, pen);
        HGDIOBJ oldBrush = SelectObject(dc_, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc_, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc_, oldBrush);
        SelectObject(dc_, oldPen);
        DeleteObject(pen);
    }

    void drawText(std::string_view text, float x, float y, Color color) override {
        // The shared overlay uses UTF-8-like ASCII labels; convert them for GDI's wide API.
        const std::wstring wide = widen(text);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(static_cast<BYTE>(color.red * 255.0F),
                              static_cast<BYTE>(color.green * 255.0F),
                              static_cast<BYTE>(color.blue * 255.0F)));
        TextOutW(dc_, static_cast<int>(x), static_cast<int>(y),
                 wide.c_str(), static_cast<int>(wide.size()));
    }

    float measureText(std::string_view text) const override {
        // The overlay asks for text width so it can center the label in the button.
        const std::wstring wide = widen(text);
        SIZE size{};
        GetTextExtentPoint32W(dc_, wide.c_str(), static_cast<int>(wide.size()), &size);
        return static_cast<float>(size.cx);
    }

private:
    static std::wstring widen(std::string_view text) {
        // All overlay labels are ASCII, so widening each byte is sufficient here.
        return std::wstring(text.begin(), text.end());
    }

    HDC dc_;
};

void drawText(HDC dc, std::wstring_view text, int x, int y, COLORREF color,
              int fontHeight = 18, bool bold = false) {
    // Create a temporary UI font, draw one label, then restore and release the GDI font.
    HFONT font = CreateFontW(fontHeight, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.data(), static_cast<int>(text.size()));
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void drawFilledPolygon(HDC dc, const std::vector<POINT>& points, COLORREF color) {
    // GDI polygons are the basic building block for the flat-shaded sample room.
    if (points.size() < 3) {
        return;
    }
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, darker(color, 0.62F));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

ScreenPoint projectPoint(Point3 point, const CameraFrame& camera,
                         Point3 right, Point3 up, Point3 forward,
                         int left, int top, int width, int height) {
    // Express the world point relative to the camera, then measure it on camera axes.
    const Point3 relative = subtract(point, asPoint(camera.eye));
    const float horizontal = dot(relative, right);
    const float vertical = dot(relative, up);
    const float depth = dot(relative, forward);
    if (camera.orthographic) {
        // Orthographic views keep apparent object size constant with distance.
        const float halfWidth = std::max(camera.orthographicHalfWidth, 0.01F);
        const float halfHeight = std::max(camera.orthographicHalfHeight, 0.01F);
        return {
            left + static_cast<int>((0.5F + horizontal / (2.0F * halfWidth)) * width),
            top + static_cast<int>((0.5F - vertical / (2.0F * halfHeight)) * height),
            depth,
            true
        };
    }

    if (depth <= 0.15F) {
        // Avoid dividing by zero and avoid drawing points behind the camera.
        return {0, 0, depth, false};
    }
    // Perspective projection makes distant objects smaller, like a normal 3D camera.
    const float focal = static_cast<float>(height) /
                        (2.0F * std::tan(camera.verticalFieldOfViewDegrees * pi / 360.0F));
    return {
        left + width / 2 + static_cast<int>(horizontal * focal / depth),
        top + height / 2 - static_cast<int>(vertical * focal / depth),
        depth,
        true
    };
}

void drawFace(HDC dc, const DrawFace& face, const CameraFrame& camera,
              Point3 right, Point3 up, Point3 forward,
              int left, int top, int width, int height) {
    // Project every corner; skip the face if any corner is behind the camera.
    std::vector<POINT> polygon;
    polygon.reserve(face.corners.size());
    for (Point3 corner : face.corners) {
        const ScreenPoint point = projectPoint(corner, camera, right, up, forward,
                                               left, top, width, height);
        if (!point.visible) {
            return;
        }
        polygon.push_back({point.x, point.y});
    }
    drawFilledPolygon(dc, polygon, face.color);
}

void drawBox(HDC dc, Point3 center, Point3 halfSize, const CameraFrame& camera,
             Point3 right, Point3 up, Point3 forward,
             int left, int top, int width, int height, COLORREF color) {
    // A box is represented by eight corners and six independently shaded faces.
    const float x0 = center.x - halfSize.x;
    const float x1 = center.x + halfSize.x;
    const float y0 = center.y - halfSize.y;
    const float y1 = center.y + halfSize.y;
    const float z0 = center.z - halfSize.z;
    const float z1 = center.z + halfSize.z;
    const Point3 p000{x0, y0, z0};
    const Point3 p001{x0, y0, z1};
    const Point3 p010{x0, y1, z0};
    const Point3 p011{x0, y1, z1};
    const Point3 p100{x1, y0, z0};
    const Point3 p101{x1, y0, z1};
    const Point3 p110{x1, y1, z0};
    const Point3 p111{x1, y1, z1};
    const std::array<DrawFace, 6> faces{{
        {{{p000, p001, p011, p010}}, darker(color, 0.73F)},
        {{{p100, p110, p111, p101}}, darker(color, 0.86F)},
        {{{p000, p100, p101, p001}}, darker(color, 0.60F)},
        {{{p010, p011, p111, p110}}, darker(color, 1.15F)},
        {{{p001, p101, p111, p011}}, color},
        {{{p000, p010, p110, p100}}, darker(color, 0.52F)}
    }};
    // Painting each visible face with a different shade suggests simple 3D depth.
    for (const DrawFace& face : faces) {
        drawFace(dc, face, camera, right, up, forward, left, top, width, height);
    }
}

void drawSphere(HDC dc, Point3 center, float radius, const CameraFrame& camera,
                Point3 right, Point3 up, Point3 forward,
                int left, int top, int width, int height, COLORREF color) {
    // Relics and the placeholder sorcerer's head are circles sized by camera distance.
    const ScreenPoint point = projectPoint(center, camera, right, up, forward,
                                           left, top, width, height);
    if (!point.visible) {
        return;
    }
    float pixelRadius = radius * 9.0F;
    if (!camera.orthographic) {
        pixelRadius = radius * height /
                      (2.0F * std::tan(camera.verticalFieldOfViewDegrees * pi / 360.0F) *
                       std::max(point.depth, 0.2F));
    } else {
        pixelRadius = radius * height /
                      (2.0F * std::max(camera.orthographicHalfHeight, 0.01F));
    }
    const int r = std::clamp(static_cast<int>(pixelRadius), 3, 46);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, point.x - r, point.y - r, point.x + r, point.y + r);
    SelectObject(dc, oldPen);
    SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    DeleteObject(brush);
}

void drawScene(HDC dc, int width, int height) {
    // Reserve room above and below for controls/status; draw the world in the middle.
    const int sceneLeft = 44;
    const int sceneTop = 112;
    const int sceneWidth = std::max(100, width - 88);
    const int sceneHeight = std::max(100, height - 220);
    // Ask the controller for the active view and create its local right/up/forward basis.
    const CameraFrame camera = navigation.camera(static_cast<float>(sceneWidth) /
                                                  static_cast<float>(sceneHeight));
    const Point3 eye = asPoint(camera.eye);
    const Point3 forward = normalized(subtract(asPoint(camera.target), eye));
    const Point3 right = normalized(cross(forward, asPoint(camera.up)));
    const Point3 up = normalized(cross(right, forward));
    const sanctum::SceneLayout& layout = navigation.sceneLayout();

    // Paint the viewport background before adding the floor, walls, and furniture.
    RECT sceneRect{sceneLeft, sceneTop, sceneLeft + sceneWidth, sceneTop + sceneHeight};
    HBRUSH sceneBrush = CreateSolidBrush(RGB(22, 18, 33));
    FillRect(dc, &sceneRect, sceneBrush);
    DeleteObject(sceneBrush);

    // Draw the room shell first so the furniture and sorcerer remain readable.
    const float x0 = layout.minX;
    const float x1 = layout.maxX;
    const float z0 = layout.minZ;
    const float z1 = layout.maxZ;
    const float wallTop = layout.height;
    drawFace(dc, {{{{x0, 0.0F, z0}, {x1, 0.0F, z0},
                    {x1, 0.0F, z1}, {x0, 0.0F, z1}}}, RGB(53, 43, 61)},
             camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight);
    drawFace(dc, {{{{x0, 0.0F, z0}, {x1, 0.0F, z0},
                    {x1, wallTop, z0}, {x0, wallTop, z0}}}, RGB(45, 37, 54)},
             camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight);
    drawFace(dc, {{{{x0, 0.0F, z1}, {x0, 0.0F, z0},
                    {x0, wallTop, z0}, {x0, wallTop, z1}}}, RGB(36, 31, 48)},
             camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight);

    if (navigation.scene() == SceneId::Library) {
        // Placeholder Library: three shelves, a reading desk, and oversized books.
        const COLORREF wood = RGB(105, 61, 43);
        const COLORREF shelf = RGB(138, 82, 54);
        for (int row = 0; row < 3; ++row) {
            const float z = -7.0F + row * 5.0F;
            drawBox(dc, {-6.8F, 2.0F, z}, {1.0F, 2.0F, 0.8F},
                    camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight, wood);
            for (int level = 0; level < 4; ++level) {
                drawBox(dc, {-6.8F, 0.55F + level * 0.9F, z + 0.83F},
                        {0.94F, 0.055F, 0.08F}, camera, right, up, forward,
                        sceneLeft, sceneTop, sceneWidth, sceneHeight, shelf);
            }
        }
        drawBox(dc, {0.0F, 0.95F, 1.0F}, {2.1F, 0.16F, 1.15F},
                camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight,
                RGB(122, 70, 48));
        drawBox(dc, {0.0F, 0.45F, 1.0F}, {0.15F, 0.48F, 0.15F},
                camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight, wood);
        drawBox(dc, {-0.8F, 1.2F, 0.8F}, {0.35F, 0.24F, 0.28F},
                camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight,
                RGB(93, 73, 125));
        drawBox(dc, {0.55F, 1.18F, 1.1F}, {0.26F, 0.2F, 0.22F},
                camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight,
                RGB(185, 143, 69));
    } else {
        // Placeholder Relic Room: four colored relics on pedestals and a rune pattern.
        const COLORREF stone = RGB(81, 72, 103);
        const std::array<Point3, 4> pedestals{{
            {-4.3F, 0.5F, -3.2F}, {4.0F, 0.5F, -3.0F},
            {-4.0F, 0.5F, 3.5F}, {4.2F, 0.5F, 3.7F}
        }};
        const std::array<COLORREF, 4> relicColors{{
            RGB(101, 193, 231), RGB(208, 112, 220),
            RGB(242, 173, 74), RGB(115, 224, 160)
        }};
        for (std::size_t i = 0; i < pedestals.size(); ++i) {
            drawBox(dc, pedestals[i], {0.9F, 0.5F, 0.9F}, camera, right, up, forward,
                    sceneLeft, sceneTop, sceneWidth, sceneHeight, stone);
            drawSphere(dc, {pedestals[i].x, 1.55F, pedestals[i].z}, 0.48F,
                       camera, right, up, forward, sceneLeft, sceneTop, sceneWidth,
                       sceneHeight, relicColors[i]);
        }
        for (int i = 0; i < 3; ++i) {
            drawBox(dc, {0.0F, 0.02F, 0.0F}, {3.8F + i * 0.55F, 0.025F, 0.055F},
                    camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight,
                    RGB(110, 75, 156));
            drawBox(dc, {0.0F, 0.02F, 0.0F}, {0.055F, 0.025F, 3.8F + i * 0.55F},
                    camera, right, up, forward, sceneLeft, sceneTop, sceneWidth, sceneHeight,
                    RGB(110, 75, 156));
        }
    }

    if (navigation.view() == ViewMode::Perspective &&
        navigation.perspectiveStyle() == sanctum::PerspectiveStyle::ThirdPerson) {
        // A few primitive shapes stand in for the sorcerer until a rigged model is connected.
        const Vec3 player = navigation.playerPosition();
        drawBox(dc, {player.x, player.y + 0.95F, player.z},
                {0.36F, 0.72F, 0.3F}, camera, right, up, forward,
                sceneLeft, sceneTop, sceneWidth, sceneHeight, RGB(108, 70, 157));
        drawSphere(dc, {player.x, player.y + 1.83F, player.z},
                   0.28F, camera, right, up, forward,
                   sceneLeft, sceneTop, sceneWidth, sceneHeight, RGB(220, 184, 158));
        drawBox(dc, {player.x, player.y + 0.88F, player.z - 0.34F},
                {0.55F, 0.16F, 0.19F}, camera, right, up, forward,
                sceneLeft, sceneTop, sceneWidth, sceneHeight, RGB(151, 54, 69));
    }

    if (navigation.view() == ViewMode::Perspective &&
        navigation.perspectiveStyle() == sanctum::PerspectiveStyle::FirstPerson) {
        // First person hides the player's body and uses a small center-screen aiming marker.
        const int cx = sceneLeft + sceneWidth / 2;
        const int cy = sceneTop + sceneHeight / 2;
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(239, 222, 255));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - 8, cy, nullptr);
        LineTo(dc, cx + 9, cy);
        MoveToEx(dc, cx, cy - 8, nullptr);
        LineTo(dc, cx, cy + 9);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    // Outline the viewport so it remains distinct from the surrounding HUD.
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(99, 76, 128));
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, sceneLeft, sceneTop, sceneLeft + sceneWidth, sceneTop + sceneHeight);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(borderPen);
}

std::wstring viewName(ViewMode view) {
    // Friendly label used by the status line below the viewport.
    switch (view) {
    case ViewMode::Top: return L"Top";
    case ViewMode::Front: return L"Front";
    case ViewMode::Side: return L"Side";
    case ViewMode::Perspective: return L"Perspective";
    }
    return L"Perspective";
}

std::array<Button, 2> sceneButtons(int windowWidth) {
    // Keep room-selection buttons aligned at the top-right of the window.
    constexpr int buttonWidth = 144;
    constexpr int buttonHeight = 34;
    constexpr int gap = 10;
    const int startX = std::max(16, windowWidth - (buttonWidth * 2 + gap + 24));
    return {{
        {{static_cast<float>(startX), 62.0F, static_cast<float>(buttonWidth),
          static_cast<float>(buttonHeight)}, L"1  LIBRARY",
         navigation.scene() == SceneId::Library},
        {{static_cast<float>(startX + buttonWidth + gap), 62.0F,
          static_cast<float>(buttonWidth), static_cast<float>(buttonHeight)},
         L"2  RELIC ROOM", navigation.scene() == SceneId::RelicRoom}
    }};
}

void drawSceneButton(HDC dc, const Button& button) {
    // The active room button is brighter so it's clear which room is displayed.
    RECT rect{
        static_cast<LONG>(button.bounds.x), static_cast<LONG>(button.bounds.y),
        static_cast<LONG>(button.bounds.x + button.bounds.width),
        static_cast<LONG>(button.bounds.y + button.bounds.height)
    };
    HBRUSH brush = CreateSolidBrush(button.selected ? RGB(85, 49, 126) : RGB(34, 28, 47));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, button.selected ? RGB(177, 130, 225) : RGB(83, 69, 103));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    drawText(dc, button.label, rect.left + 12, rect.top + 8,
             RGB(243, 232, 255), 15, true);
}

void drawHud(HDC dc, int width, int height) {
    // Draw the title, clickable controls, current view, coordinates, and key hints.
    drawText(dc, L"DOCTOR STRANGE'S SANCTUM", 44, 18, RGB(236, 218, 255), 25, true);
    drawText(dc, L"PERSON 1  /  VIEW + SCENE NAVIGATION DEMO",
             46, 48, RGB(164, 143, 190), 13, true);
    for (const Button& button : sceneButtons(width)) {
        drawSceneButton(dc, button);
    }

    // Reuse ViewOverlay from the navigation library rather than duplicating button layout.
    GdiCanvas canvas(dc);
    viewOverlay.draw(canvas, static_cast<float>(width), navigation.view());

    // Describe the sample geometry in the currently selected room.
    const std::wstring scene = navigation.scene() == SceneId::Library
                                   ? L"LIBRARY  |  shelves, reading desk, spellbooks"
                                   : L"RELIC ROOM  |  four glowing relics and a rune floor";
    drawText(dc, scene, 46, 91, RGB(218, 203, 236), 15, true);

    std::wstring mode = L"CAMERA: " + viewName(navigation.view());
    if (navigation.view() == ViewMode::Perspective) {
        mode += navigation.perspectiveStyle() == sanctum::PerspectiveStyle::FirstPerson
                    ? L"  /  FIRST PERSON"
                    : L"  /  THIRD PERSON";
    }
    drawText(dc, mode, 46, height - 91, RGB(221, 193, 255), 16, true);

    // These coordinates are useful even in fixed views, where the sample avatar is hidden.
    const Vec3 player = navigation.playerPosition();
    wchar_t position[128]{};
    swprintf_s(position, L"PLAYER X: %5.1f    Z: %5.1f    ROOM BOUNDS: X [%g, %g]  Z [%g, %g]",
               player.x, player.z, navigation.sceneLayout().minX, navigation.sceneLayout().maxX,
               navigation.sceneLayout().minZ, navigation.sceneLayout().maxZ);
    drawText(dc, position, 46, height - 65, RGB(174, 160, 191), 13, false);
    drawText(dc, L"WASD move   |   Arrows look   |   F1-F4 views   |   Tab switches rooms   |   N reset camera",
             46, height - 39, RGB(174, 160, 191), 13, false);
    drawText(dc, L"Click the view or room buttons. This demo uses simple drawn geometry, not the final 3D assets.",
             46, height - 19, RGB(126, 112, 144), 12, false);
}

void updateMovementKeys() {
    // Forward all four held states together so pressing a second movement key
    // does not accidentally release the first one.
    navigation.onKey(sanctum::Key::W, movementKeys[0]);
    navigation.onKey(sanctum::Key::A, movementKeys[1]);
    navigation.onKey(sanctum::Key::S, movementKeys[2]);
    navigation.onKey(sanctum::Key::D, movementKeys[3]);
}

void paintWindow(HWND window) {
    // WM_PAINT is the Windows request to redraw. Render off-screen to reduce flicker.
    PAINTSTRUCT paint{};
    HDC windowDc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC bufferDc = CreateCompatibleDC(windowDc);
    HBITMAP buffer = CreateCompatibleBitmap(windowDc, width, height);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, buffer);

    HBRUSH background = CreateSolidBrush(backgroundColor);
    FillRect(bufferDc, &client, background);
    DeleteObject(background);
    // The HUD occupies reserved margins; draw the world into the central viewport.
    drawHud(bufferDc, width, height);
    drawScene(bufferDc, width, height);

    BitBlt(windowDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
    SelectObject(bufferDc, oldBitmap);
    DeleteObject(buffer);
    DeleteDC(bufferDc);
    EndPaint(window, &paint);
}

void handleKey(HWND window, WPARAM key, bool pressed) {
    // Translate Win32 virtual key codes into the controller's portable Key values.
    switch (key) {
    case 'W': movementKeys[0] = pressed; updateMovementKeys(); break;
    case 'A': movementKeys[1] = pressed; updateMovementKeys(); break;
    case 'S': movementKeys[2] = pressed; updateMovementKeys(); break;
    case 'D': movementKeys[3] = pressed; updateMovementKeys(); break;
    case VK_LEFT: navigation.onKey(sanctum::Key::ArrowLeft, pressed); break;
    case VK_RIGHT: navigation.onKey(sanctum::Key::ArrowRight, pressed); break;
    case VK_UP: navigation.onKey(sanctum::Key::ArrowUp, pressed); break;
    case VK_DOWN: navigation.onKey(sanctum::Key::ArrowDown, pressed); break;
    default:
        if (!pressed) {
            // Scene/view shortcuts are one-shot, so releases need no further processing.
            return;
        }
        switch (key) {
        case VK_F1: navigation.onKey(sanctum::Key::Top, true); navigation.onKey(sanctum::Key::Top, false); break;
        case VK_F2: navigation.onKey(sanctum::Key::Front, true); navigation.onKey(sanctum::Key::Front, false); break;
        case VK_F3: navigation.onKey(sanctum::Key::Side, true); navigation.onKey(sanctum::Key::Side, false); break;
        case VK_F4: navigation.onKey(sanctum::Key::Perspective, true); navigation.onKey(sanctum::Key::Perspective, false); break;
        case VK_TAB: navigation.onKey(sanctum::Key::ToggleScene, true); navigation.onKey(sanctum::Key::ToggleScene, false); break;
        case '1': navigation.onKey(sanctum::Key::Library, true); navigation.onKey(sanctum::Key::Library, false); break;
        case '2': navigation.onKey(sanctum::Key::RelicRoom, true); navigation.onKey(sanctum::Key::RelicRoom, false); break;
        case 'N': navigation.onKey(sanctum::Key::ReturnToNavigation, true); navigation.onKey(sanctum::Key::ReturnToNavigation, false); break;
        case VK_ESCAPE: DestroyWindow(window); break;
        default: break;
        }
        break;
    }
    InvalidateRect(window, nullptr, FALSE);
}

void handleClick(HWND window, int x, int y) {
    // Try the four view buttons first, then the two room-selection buttons.
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    if (viewOverlay.handleClick(navigation, static_cast<float>(x),
                                static_cast<float>(y), static_cast<float>(width))) {
        InvalidateRect(window, nullptr, FALSE);
        return;
    }

    for (const Button& button : sceneButtons(width)) {
        const Rect& rect = button.bounds;
        if (x >= rect.x && x <= rect.x + rect.width &&
            y >= rect.y && y <= rect.y + rect.height) {
            // Clicking the other room switches to it; clicking the active room is a no-op.
            navigation.selectScene(button.selected ? navigation.scene()
                                                    : (navigation.scene() == SceneId::Library
                                                           ? SceneId::RelicRoom : SceneId::Library));
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
    }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Windows sends all keyboard, mouse, timer, and redraw events through this callback.
    switch (message) {
    case WM_PAINT:
        paintWindow(window);
        return 0;
    case WM_KEYDOWN:
        // Windows repeats key-down messages while held; ignore repeats for one-shot shortcuts.
        if ((lParam & (1LL << 30)) == 0) {
            handleKey(window, wParam, true);
        }
        return 0;
    case WM_KEYUP:
        handleKey(window, wParam, false);
        return 0;
    case WM_KILLFOCUS:
        // Prevent a stuck movement/look key if the user switches to another application.
        movementKeys.fill(false);
        updateMovementKeys();
        for (sanctum::Key key : {sanctum::Key::ArrowLeft, sanctum::Key::ArrowRight,
                                 sanctum::Key::ArrowUp, sanctum::Key::ArrowDown}) {
            navigation.onKey(key, false);
        }
        return 0;
    case WM_LBUTTONDOWN:
        // Mouse coordinates are already client-area coordinates, matching ViewOverlay.
        handleClick(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        SetFocus(window);
        return 0;
    case WM_TIMER: {
        // A 16 ms timer approximates 60 FPS; controller movement still uses actual elapsed time.
        const auto now = std::chrono::steady_clock::now();
        const float delta = std::chrono::duration<float>(now - previousFrame).count();
        previousFrame = now;
        navigation.update(std::min(delta, 0.1F));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        // WM_PAINT fills the background itself, so skip a redundant erase/flicker.
        return 1;
    case WM_DESTROY:
        // Stop periodic redraws and tell the message loop to exit.
        KillTimer(window, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    // Register the window type, create one resizable window, then process its events.
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = CreateSolidBrush(backgroundColor);
    windowClass.lpszClassName = windowClassName;
    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"Could not register the Sanctum demo window.",
                    L"Sanctum Navigation Demo", MB_ICONERROR | MB_OK);
        return 1;
    }

    HWND window = CreateWindowExW(
        0, windowClassName, L"Doctor Strange's Sanctum - Navigation Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the Sanctum demo window.",
                    L"Sanctum Navigation Demo", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    // Initialize elapsed-time tracking only after the window is ready.
    previousFrame = std::chrono::steady_clock::now();
    SetTimer(window, 1, 16, nullptr);

    MSG message{};
    // Standard Win32 event loop: dispatch user input and timer/paint messages.
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
