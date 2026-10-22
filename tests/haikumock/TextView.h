#ifndef MOCK_TEXTVIEW_H
#define MOCK_TEXTVIEW_H
#include <View.h>
class BTextView : public BView {
public:
    BTextView(const char* name) : BView(name, B_WILL_DRAW) {}
    BTextView(BRect f, const char* name, BRect t, uint32 rm, uint32 fl)
        : BView(name, fl) { (void)f;(void)t;(void)rm; }
    void SetText(const char* t) { fText = t ? t : ""; }
    const char* Text() const { return fText.String(); }
    int32 TextLength() const { return fText.Length(); }
    void Insert(const char* t) { fText << t; }
    void Insert(int32, const char* t, int32) { fText << t; }
    void MakeEditable(bool) {}
    void MakeSelectable(bool) {}
    void SetStylable(bool) {}
    void SetWordWrap(bool) {}
    void SetInsets(float, float, float, float) {}
    void HideTyping(bool) {}
    void ScrollToOffset(int32) {}
    void ScrollToSelection() {}
    void SetFontAndColor(const BFont*, uint32 = B_FONT_ALL, const rgb_color* = 0) {}
    void SetFontAndColor(int32, int32, const BFont*, uint32 = B_FONT_ALL, const rgb_color* = 0) {}
    void SelectAll() {}
private:
    BString fText;
};
#endif
