#ifndef MOCK_CARDLAYOUT_H
#define MOCK_CARDLAYOUT_H
#include <Layout.h>
class BCardLayout : public BLayout {
public:
    BCardLayout() : fVisible(0) {}
    void SetVisibleItem(int32 i) { fVisible = i; }
    int32 VisibleIndex() const { return fVisible; }
private:
    int32 fVisible;
};
#endif
