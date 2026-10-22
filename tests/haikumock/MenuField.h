#ifndef MOCK_MENUFIELD_H
#define MOCK_MENUFIELD_H
#include <View.h>
#include <Menu.h>
class BMenuField : public BView {
public:
    BMenuField(const char* name, const char* label, BMenu* menu)
        : BView(name, 0), fMenu(menu) { (void)label; }
    BMenu* Menu() const { return fMenu; }
    void SetEnabled(bool) {}
private:
    BMenu* fMenu;
};
#endif
