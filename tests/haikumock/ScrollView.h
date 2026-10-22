#ifndef MOCK_SCROLLVIEW_H
#define MOCK_SCROLLVIEW_H
#include <View.h>
class BScrollBar;
class BScrollView : public BView {
public:
    BScrollView(const char* name, BView* target, uint32 flags = 0, bool h = false,
        bool v = false, uint32 border = 0)
        : BView(name, B_WILL_DRAW) { (void)target;(void)flags;(void)h;(void)v;(void)border; }
    BScrollBar* ScrollBar(orientation) const { return 0; }
};
#endif
