// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_MAPVIEW_H
#define MESHY_HAIKU_MAPVIEW_H

#include <View.h>

#include <string>
#include <vector>

#include "Engine.h"

class BButton;
class BStringView;

// One plotted node.
struct MapNode {
    std::string key;
    std::string label;
    double lat;
    double lon;
    int type;
    bool is_self;
    bool is_discovered;
};

// Draws the nodes on an equirectangular projection with a graticule.  Haiku
// has no libshumate, so there are no raster tiles — this plots relative
// positions, distances and a scale bar, which is what the map is actually
// used for.
class MapView;

class MapCanvas : public BView {
public:
    // `owner` receives the selection notices; layout builders nest views, so
    // the canvas cannot rely on Parent() being the MapView.
    MapCanvas(meshy::Engine* engine, MapView* owner);

    virtual void Draw(BRect updateRect);
    virtual void MouseDown(BPoint where);
    virtual void MouseUp(BPoint where);
    virtual void MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage);
    virtual void ScrollTo(BPoint where);
    virtual void MessageReceived(BMessage* message);
    virtual void FrameResized(float width, float height);

    void Reload();
    void FitAll();
    void ZoomBy(double factor);
    // Pixels per degree of longitude at the current zoom.
    double Scale() const { return fScale; }

private:
    BPoint _Project(double lat, double lon) const;
    void _DrawGraticule(BRect bounds);
    void _DrawScaleBar(BRect bounds);
    void _DrawNode(const MapNode& node, bool selected);

    meshy::Engine* fEngine;
    MapView* fOwner;
    std::vector<MapNode> fNodes;
    double fCenterLat;
    double fCenterLon;
    double fScale;
    bool fDragging;
    BPoint fLastMouse;
    int fSelected;
};

class MapView : public BView {
public:
    explicit MapView(meshy::Engine* engine);
    virtual ~MapView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void Reload();

private:
    meshy::Engine* fEngine;
    MapCanvas* fCanvas;
    BStringView* fInfoView;
};

#endif  // MESHY_HAIKU_MAPVIEW_H
