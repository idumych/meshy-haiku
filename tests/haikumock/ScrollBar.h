#ifndef MOCK_SCROLLBAR_H
#define MOCK_SCROLLBAR_H
#include <View.h>
class BScrollBar : public BView {
public:
    BScrollBar(const char* n) : BView(n, 0) {}
    void SetRange(float, float) {}
    void SetValue(float) {}
    float Value() const { return 0; }
    void GetRange(float* a, float* b) const { *a=0; *b=0; }
};
#endif
