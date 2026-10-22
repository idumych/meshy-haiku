#ifndef MOCK_BUTTON_H
#define MOCK_BUTTON_H
#include <Control.h>
class BButton : public BControl {
public:
    BButton(const char* name, const char* label, BMessage* m) : BControl(name, label, m) {}
};
#endif
