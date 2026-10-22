#ifndef MOCK_FONT_H
#define MOCK_FONT_H
#include <SupportDefs.h>
struct font_height { float ascent, descent, leading; };
class BFont {
public:
    BFont() : fSize(12) {}
    BFont(const BFont* o) : fSize(o ? o->fSize : 12) {}
    BFont(const BFont& o) : fSize(o.fSize) {}
    float Size() const { return fSize; }
    void SetSize(float s) { fSize = s; }
    void GetHeight(font_height* h) const { h->ascent=10; h->descent=3; h->leading=1; }
    float StringWidth(const char*) const { return 50; }
private:
    float fSize;
};
extern const BFont* be_plain_font;
extern const BFont* be_bold_font;
extern const BFont* be_fixed_font;
#define B_FONT_ALL 0xffff
#endif
