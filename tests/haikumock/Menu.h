#ifndef MOCK_MENU_H
#define MOCK_MENU_H
#include <View.h>
class BMenuItem; class BHandler;
class BMenu : public BView {
public:
    BMenu(const char* name) : BView(name, 0) {}
    bool AddItem(BMenuItem*) { return true; }
    bool AddItem(BMenu*) { return true; }
    void AddSeparatorItem() {}
    BMenuItem* FindMarked() const { return 0; }
    int32 IndexOf(BMenuItem*) const { return 0; }
    BMenuItem* ItemAt(int32) const { return 0; }
    int32 CountItems() const { return 0; }
    void SetTargetForItems(BHandler*) {}
    void SetRadioMode(bool) {}
    void SetLabelFromMarked(bool) {}
};
#endif
