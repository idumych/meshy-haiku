#ifndef MOCK_LAYOUT_H
#define MOCK_LAYOUT_H
#include <View.h>
class BLayoutItem { public: virtual ~BLayoutItem() {} };
class BLayout {
public:
    virtual ~BLayout() {}
    BLayoutItem* AddView(BView*) { return 0; }
    BLayoutItem* AddView(int32, BView*) { return 0; }
    void SetInsets(float, float, float, float) {}
};
#endif
