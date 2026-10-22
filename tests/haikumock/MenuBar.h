#ifndef MOCK_MENUBAR_H
#define MOCK_MENUBAR_H
#include <Menu.h>
class BMenuBar : public BMenu {
public:
    BMenuBar(const char* name) : BMenu(name) {}
};
#endif
