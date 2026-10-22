#ifndef MOCK_STRINGVIEW_H
#define MOCK_STRINGVIEW_H
#include <View.h>
class BStringView : public BView {
public:
    BStringView(const char* name, const char* text) : BView(name, B_WILL_DRAW), fText(text?text:"") {}
    void SetText(const char* t) { fText = t ? t : ""; }
    const char* Text() const { return fText.String(); }
    void SetAlignment(uint32) {}
private:
    BString fText;
};
#endif
