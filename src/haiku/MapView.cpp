// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MapView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

enum {
    kMsgMapFit = 'mpFt',
    kMsgMapZoomIn = 'mpZi',
    kMsgMapZoomOut = 'mpZo',
    kMsgMapRefresh = 'mpRf',
};

static const double kMinScale = 0.05;      // pixels per degree
static const double kMaxScale = 400000.0;

// ─── MapCanvas ───────────────────────────────────────────────────

MapCanvas::MapCanvas(Engine* engine, MapView* owner)
    : BView("mapCanvas", B_WILL_DRAW | B_FRAME_EVENTS),
      fEngine(engine),
      fOwner(owner),
      fCenterLat(0.0),
      fCenterLon(0.0),
      fScale(1000.0),
      fDragging(false),
      fSelected(-1)
{
    SetViewColor(B_TRANSPARENT_COLOR);  // fully repainted in Draw()
}

void MapCanvas::Reload()
{
    fNodes.clear();

    DeviceInfo info = fEngine->GetDeviceInfo();
    if (info.has_location) {
        MapNode n;
        n.key = info.public_key;
        n.label = info.name.empty() ? "This node" : info.name;
        n.lat = info.adv_lat;
        n.lon = info.adv_lon;
        n.type = CONTACT_CHAT;
        n.is_self = true;
        n.is_discovered = false;
        fNodes.push_back(n);
    }

    std::vector<Contact> contacts = fEngine->Store().GetContacts();
    for (size_t i = 0; i < contacts.size(); i++) {
        if (!contacts[i].has_location)
            continue;
        MapNode n;
        n.key = contacts[i].PublicKeyHex();
        n.label = contacts[i].DisplayName();
        n.lat = contacts[i].latitude;
        n.lon = contacts[i].longitude;
        n.type = contacts[i].type;
        n.is_self = false;
        n.is_discovered = false;
        fNodes.push_back(n);
    }

    std::vector<DiscoveredContact> discovered = fEngine->Store().GetDiscovered();
    for (size_t i = 0; i < discovered.size(); i++) {
        if (!discovered[i].has_location)
            continue;
        MapNode n;
        n.key = discovered[i].public_key_hex;
        n.label = discovered[i].name.empty() ? "(unnamed)" : discovered[i].name;
        n.lat = discovered[i].latitude;
        n.lon = discovered[i].longitude;
        n.type = discovered[i].type;
        n.is_self = false;
        n.is_discovered = true;
        fNodes.push_back(n);
    }

    if (fSelected >= static_cast<int>(fNodes.size()))
        fSelected = -1;
    Invalidate();
}

void MapCanvas::FitAll()
{
    if (fNodes.empty()) {
        fCenterLat = 0.0;
        fCenterLon = 0.0;
        fScale = 2.0;
        Invalidate();
        return;
    }

    double minLat = fNodes[0].lat, maxLat = fNodes[0].lat;
    double minLon = fNodes[0].lon, maxLon = fNodes[0].lon;
    for (size_t i = 1; i < fNodes.size(); i++) {
        if (fNodes[i].lat < minLat) minLat = fNodes[i].lat;
        if (fNodes[i].lat > maxLat) maxLat = fNodes[i].lat;
        if (fNodes[i].lon < minLon) minLon = fNodes[i].lon;
        if (fNodes[i].lon > maxLon) maxLon = fNodes[i].lon;
    }
    fCenterLat = (minLat + maxLat) / 2.0;
    fCenterLon = (minLon + maxLon) / 2.0;

    BRect b = Bounds();
    double spanLat = maxLat - minLat;
    double spanLon = maxLon - minLon;
    // A single node, or several at one spot, gets a sensible default zoom.
    if (spanLat < 1e-6 && spanLon < 1e-6) {
        fScale = 20000.0;
    } else {
        double cosLat = cos(fCenterLat * M_PI / 180.0);
        if (cosLat < 0.01)
            cosLat = 0.01;
        double scaleY = spanLat > 1e-9 ? (b.Height() * 0.8) / spanLat : kMaxScale;
        double scaleX = spanLon > 1e-9 ? (b.Width() * 0.8) / (spanLon * cosLat) : kMaxScale;
        fScale = scaleX < scaleY ? scaleX : scaleY;
    }
    if (fScale < kMinScale) fScale = kMinScale;
    if (fScale > kMaxScale) fScale = kMaxScale;
    Invalidate();
}

void MapCanvas::ZoomBy(double factor)
{
    fScale *= factor;
    if (fScale < kMinScale) fScale = kMinScale;
    if (fScale > kMaxScale) fScale = kMaxScale;
    Invalidate();
}

BPoint MapCanvas::_Project(double lat, double lon) const
{
    BRect b = Bounds();
    double cosLat = cos(fCenterLat * M_PI / 180.0);
    if (cosLat < 0.01)
        cosLat = 0.01;
    double x = b.Width() / 2.0 + (lon - fCenterLon) * cosLat * fScale;
    double y = b.Height() / 2.0 - (lat - fCenterLat) * fScale;
    return BPoint(static_cast<float>(x), static_cast<float>(y));
}

void MapCanvas::_DrawGraticule(BRect bounds)
{
    // Choose a round degree step that gives a handful of lines on screen.
    static const double kSteps[] = { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02,
        0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 45.0 };
    double step = kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
    for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); i++) {
        if (kSteps[i] * fScale >= 60.0) {
            step = kSteps[i];
            break;
        }
    }

    rgb_color grid = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT);
    SetHighColor(grid);
    SetFontSize(ceilf(be_plain_font->Size() * 0.85f));

    double halfLatSpan = (bounds.Height() / 2.0) / fScale;
    double cosLat = cos(fCenterLat * M_PI / 180.0);
    if (cosLat < 0.01)
        cosLat = 0.01;
    double halfLonSpan = (bounds.Width() / 2.0) / (fScale * cosLat);

    double startLat = floor((fCenterLat - halfLatSpan) / step) * step;
    for (double lat = startLat; lat <= fCenterLat + halfLatSpan; lat += step) {
        BPoint p = _Project(lat, fCenterLon);
        if (p.y < bounds.top || p.y > bounds.bottom)
            continue;
        StrokeLine(BPoint(bounds.left, p.y), BPoint(bounds.right, p.y));
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g°", lat);
        DrawString(buf, BPoint(bounds.left + 4, p.y - 3));
    }

    double startLon = floor((fCenterLon - halfLonSpan) / step) * step;
    for (double lon = startLon; lon <= fCenterLon + halfLonSpan; lon += step) {
        BPoint p = _Project(fCenterLat, lon);
        if (p.x < bounds.left || p.x > bounds.right)
            continue;
        StrokeLine(BPoint(p.x, bounds.top), BPoint(p.x, bounds.bottom));
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g°", lon);
        DrawString(buf, BPoint(p.x + 3, bounds.bottom - 4));
    }
    SetFontSize(be_plain_font->Size());
}

void MapCanvas::_DrawScaleBar(BRect bounds)
{
    // One degree of latitude is about 111.32 km everywhere.
    double metersPerPixel = 111320.0 / fScale;
    double targetMeters = metersPerPixel * 120.0;

    static const double kNice[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000,
        50000, 100000, 200000, 500000, 1000000, 2000000 };
    double chosen = kNice[0];
    for (size_t i = 0; i < sizeof(kNice) / sizeof(kNice[0]); i++) {
        if (kNice[i] <= targetMeters)
            chosen = kNice[i];
    }
    float px = static_cast<float>(chosen / metersPerPixel);
    if (px < 10.0f || px > bounds.Width())
        return;

    float y = bounds.bottom - 28.0f;
    float x = bounds.right - px - 16.0f;
    SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
    StrokeLine(BPoint(x, y), BPoint(x + px, y));
    StrokeLine(BPoint(x, y - 4), BPoint(x, y + 4));
    StrokeLine(BPoint(x + px, y - 4), BPoint(x + px, y + 4));

    char buf[32];
    if (chosen >= 1000.0)
        snprintf(buf, sizeof(buf), "%.0f km", chosen / 1000.0);
    else
        snprintf(buf, sizeof(buf), "%.0f m", chosen);
    DrawString(buf, BPoint(x, y - 8));
}

void MapCanvas::_DrawNode(const MapNode& node, bool selected)
{
    BPoint p = _Project(node.lat, node.lon);
    BRect b = Bounds();
    if (!b.Contains(p))
        return;

    // Colour by role so the mesh topology reads at a glance.
    rgb_color fill;
    if (node.is_self)
        fill = (rgb_color){ 40, 140, 220, 255 };
    else if (node.is_discovered)
        fill = (rgb_color){ 150, 150, 150, 255 };
    else switch (node.type) {
        case CONTACT_REPEATER: fill = (rgb_color){ 220, 150, 40, 255 }; break;
        case CONTACT_ROOM: fill = (rgb_color){ 140, 100, 200, 255 }; break;
        case CONTACT_SENSOR: fill = (rgb_color){ 60, 170, 90, 255 }; break;
        default: fill = (rgb_color){ 80, 80, 80, 255 }; break;
    }

    float r = node.is_self ? 7.0f : 5.0f;
    if (selected)
        r += 3.0f;

    SetHighColor(fill);
    FillEllipse(p, r, r);
    SetHighColor(selected ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
                          : tint_color(fill, B_DARKEN_2_TINT));
    SetPenSize(selected ? 2.0f : 1.0f);
    StrokeEllipse(p, r, r);
    SetPenSize(1.0f);

    SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
    BString label(node.label.c_str());
    TruncateString(&label, B_TRUNCATE_END, 140.0f);
    DrawString(label.String(), BPoint(p.x + r + 4.0f, p.y + 4.0f));
}

void MapCanvas::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));
    FillRect(updateRect, B_SOLID_LOW);

    _DrawGraticule(bounds);

    if (fNodes.empty()) {
        SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR), B_LIGHTEN_1_TINT));
        const char* text = "No nodes with a known position yet.";
        float w = StringWidth(text);
        DrawString(text, BPoint((bounds.Width() - w) / 2.0f, bounds.Height() / 2.0f));
        return;
    }

    // Draw links from this node to everything else, so range is visible.
    int selfIndex = -1;
    for (size_t i = 0; i < fNodes.size(); i++) {
        if (fNodes[i].is_self) {
            selfIndex = static_cast<int>(i);
            break;
        }
    }
    if (selfIndex >= 0) {
        rgb_color link = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
        SetHighColor(link);
        BPoint origin = _Project(fNodes[selfIndex].lat, fNodes[selfIndex].lon);
        for (size_t i = 0; i < fNodes.size(); i++) {
            if (static_cast<int>(i) == selfIndex || fNodes[i].is_discovered)
                continue;
            StrokeLine(origin, _Project(fNodes[i].lat, fNodes[i].lon));
        }
    }

    for (size_t i = 0; i < fNodes.size(); i++)
        _DrawNode(fNodes[i], static_cast<int>(i) == fSelected);

    _DrawScaleBar(bounds);
}

void MapCanvas::MouseDown(BPoint where)
{
    MakeFocus(true);

    // Select the nearest node within a few pixels.
    fSelected = -1;
    float best = 14.0f;
    for (size_t i = 0; i < fNodes.size(); i++) {
        BPoint p = _Project(fNodes[i].lat, fNodes[i].lon);
        float dx = p.x - where.x;
        float dy = p.y - where.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < best) {
            best = d;
            fSelected = static_cast<int>(i);
        }
    }

    fDragging = true;
    fLastMouse = where;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
    Invalidate();

    if (fSelected >= 0 && fOwner != NULL) {
        BMessage note(kMsgMapRefresh);
        note.AddString("selected", fNodes[fSelected].label.c_str());
        note.AddString("key", fNodes[fSelected].key.c_str());
        note.AddDouble("lat", fNodes[fSelected].lat);
        note.AddDouble("lon", fNodes[fSelected].lon);
        fOwner->MessageReceived(&note);
    }
}

void MapCanvas::MouseUp(BPoint where)
{
    (void)where;
    fDragging = false;
}

void MapCanvas::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
    (void)code;
    (void)dragMessage;
    if (!fDragging)
        return;
    double cosLat = cos(fCenterLat * M_PI / 180.0);
    if (cosLat < 0.01)
        cosLat = 0.01;
    fCenterLon -= (where.x - fLastMouse.x) / (fScale * cosLat);
    fCenterLat += (where.y - fLastMouse.y) / fScale;
    fLastMouse = where;
    Invalidate();
}

void MapCanvas::ScrollTo(BPoint where)
{
    BView::ScrollTo(where);
}

void MapCanvas::FrameResized(float width, float height)
{
    BView::FrameResized(width, height);
    Invalidate();
}

void MapCanvas::MessageReceived(BMessage* message)
{
    // The wheel arrives as B_MOUSE_WHEEL_CHANGED.
    if (message->what == B_MOUSE_WHEEL_CHANGED) {
        float dy = 0.0f;
        if (message->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0f) {
            ZoomBy(dy < 0 ? 1.25 : 0.8);
            return;
        }
    }
    BView::MessageReceived(message);
}

// ─── MapView ─────────────────────────────────────────────────────

MapView::MapView(Engine* engine) : BView("map", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fCanvas = new MapCanvas(engine, this);
    fInfoView = new BStringView("mapInfo", "Drag to pan, scroll to zoom.");
    fInfoView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    BButton* fitButton = new BButton("fit", "Fit all", new BMessage(kMsgMapFit));
    BButton* zoomInButton = new BButton("zin", "+", new BMessage(kMsgMapZoomIn));
    BButton* zoomOutButton = new BButton("zout", "−", new BMessage(kMsgMapZoomOut));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fitButton)
            .Add(zoomOutButton)
            .Add(zoomInButton)
            .AddGlue()
            .Add(fInfoView)
        .End()
        .Add(fCanvas, 1.0f)
    .End();
}

MapView::~MapView() {}

void MapView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
    Reload();
}

void MapView::Reload()
{
    fCanvas->Reload();
}

void MapView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgMapFit:
            fCanvas->FitAll();
            break;
        case kMsgMapZoomIn:
            fCanvas->ZoomBy(1.4);
            break;
        case kMsgMapZoomOut:
            fCanvas->ZoomBy(1.0 / 1.4);
            break;
        case kMsgMapRefresh: {
            const char* label = NULL;
            double lat = 0.0, lon = 0.0;
            if (message->FindString("selected", &label) == B_OK) {
                message->FindDouble("lat", &lat);
                message->FindDouble("lon", &lon);
                BString s;
                s.SetToFormat("%s — %.5f, %.5f", label, lat, lon);
                fInfoView->SetText(s.String());
            }
            break;
        }
        default:
            BView::MessageReceived(message);
            break;
    }
}
