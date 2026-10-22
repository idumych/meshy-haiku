#ifndef MOCK_CHECKBOX_H
#define MOCK_CHECKBOX_H
#include <Control.h>
class BCheckBox : public BControl {
public:
    BCheckBox(const char* name, const char* label, BMessage* m) : BControl(name, label, m) {}
};
#endif
