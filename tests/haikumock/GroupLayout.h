#ifndef MOCK_GROUPLAYOUT_H
#define MOCK_GROUPLAYOUT_H
#include <Layout.h>
class BGroupLayout : public BLayout {
public:
    BGroupLayout(orientation o, float spacing = 0) { (void)o;(void)spacing; }
    BLayoutItem* AddView(BView*, float) { return 0; }
};
#endif
