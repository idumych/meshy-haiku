#ifndef MOCK_GRIDLAYOUT_H
#define MOCK_GRIDLAYOUT_H
#include <Layout.h>
class BGridLayout : public BLayout {
public:
    BGridLayout(float h = 0, float v = 0) { (void)h;(void)v; }
    BLayoutItem* AddView(BView*, int32, int32, int32 = 1, int32 = 1) { return 0; }
};
#endif
