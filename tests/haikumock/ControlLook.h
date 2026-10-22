#ifndef MOCK_CONTROLLOOK_H
#define MOCK_CONTROLLOOK_H
#include <SupportDefs.h>
class BControlLook {
public:
    float DefaultItemSpacing() const { return 14.0f; }
    float DefaultLabelSpacing() const { return 6.0f; }
};
extern BControlLook* be_control_look;
#endif
