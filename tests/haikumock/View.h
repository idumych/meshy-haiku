#ifndef MOCK_VIEW_H
#define MOCK_VIEW_H
#include <Handler.h>
#include <Rect.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <String.h>
class BLayout; class BWindow; class BScrollBar;
class BView : public BHandler {
public:
    BView(const char* name, uint32 flags) : BHandler(name) { (void)flags; }
    BView(BRect f, const char* name, uint32 rm, uint32 flags) : BHandler(name) { (void)f;(void)rm;(void)flags; }
    virtual ~BView() {}
    virtual void AttachedToWindow() {}
    virtual void AllAttached() {}
    virtual void DetachedFromWindow() {}
    virtual void Draw(BRect) {}
    virtual void MouseDown(BPoint) {}
    virtual void MouseUp(BPoint) {}
    virtual void MouseMoved(BPoint, uint32, const BMessage*) {}
    virtual void FrameResized(float, float) {}
    virtual void ScrollTo(BPoint) {}
    void ScrollTo(float x, float y) { ScrollTo(BPoint(x,y)); }
    virtual void MessageReceived(BMessage*) {}
    virtual void KeyDown(const char*, int32) {}
    BRect Bounds() const { return BRect(0,0,600,400); }
    BRect Frame() const { return BRect(0,0,600,400); }
    BWindow* Window() const { return 0; }
    BView* Parent() const { return 0; }
    int32 CountChildren() const { return 0; }
    BView* ChildAt(int32) const { return 0; }
    void AddChild(BView*) {}
    void SetLayout(BLayout*) {}
    BLayout* GetLayout() const { return 0; }
    void Invalidate() {}
    void Invalidate(BRect) {}
    void MakeFocus(bool = true) {}
    bool IsHidden() const { return false; }
    void Hide() {} void Show() {}
    void SetViewColor(rgb_color) {}
    void SetViewUIColor(color_which, float = 1.0f) {}
    void SetLowColor(rgb_color) {}
    void SetLowUIColor(color_which, float = 1.0f) {}
    void SetHighColor(rgb_color) {}
    void SetHighColor(uint8, uint8, uint8, uint8 = 255) {}
    void SetHighUIColor(color_which, float = 1.0f) {}
    rgb_color ViewColor() const { return rgb_color{0,0,0,255}; }
    rgb_color LowColor() const { return rgb_color{0,0,0,255}; }
    rgb_color HighColor() const { return rgb_color{0,0,0,255}; }
    void SetFont(const BFont*, uint32 = B_FONT_ALL) {}
    void GetFont(BFont*) const {}
    void SetFontSize(float) {}
    float StringWidth(const char*) const { return 50; }
    void TruncateString(BString*, uint32, float) const {}
    void DrawString(const char*, BPoint) {}
    void DrawString(const char*) {}
    void FillRect(BRect, int = B_SOLID_HIGH) {}
    void StrokeRect(BRect) {}
    void FillRoundRect(BRect, float, float) {}
    void StrokeLine(BPoint, BPoint) {}
    void FillEllipse(BPoint, float, float) {}
    void StrokeEllipse(BPoint, float, float) {}
    void SetPenSize(float) {}
    void SetMouseEventMask(uint32, uint32 = 0) {}
    void SetExplicitMinSize(BSize) {}
    void SetExplicitMaxSize(BSize) {}
    void SetExplicitPreferredSize(BSize) {}
    void SetExplicitAlignment(BAlignment) {}
    void SetToolTip(const char*) {}
};
#endif
