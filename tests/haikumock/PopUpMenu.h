#ifndef MOCK_POPUPMENU_H
#define MOCK_POPUPMENU_H
#include <Menu.h>
class BPopUpMenu : public BMenu {
public:
    BPopUpMenu(const char* name) : BMenu(name) {}
};
#endif
