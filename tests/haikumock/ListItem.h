#ifndef MOCK_LISTITEM_H
#define MOCK_LISTITEM_H
#include <View.h>
class BListItem {
public:
    BListItem() : fSelected(false), fHeight(16) {}
    virtual ~BListItem() {}
    virtual void DrawItem(BView*, BRect, bool = false) {}
    virtual void Update(BView*, const BFont*) {}
    bool IsSelected() const { return fSelected; }
    void SetHeight(float h) { fHeight = h; }
    float Height() const { return fHeight; }
    void SetWidth(float) {}
private:
    bool fSelected;
    float fHeight;
};
class BStringItem : public BListItem {
public:
    BStringItem(const char* t) : fText(t?t:"") {}
    void SetText(const char* t) { fText = t ? t : ""; }
    const char* Text() const { return fText.String(); }
private:
    BString fText;
};
#endif
