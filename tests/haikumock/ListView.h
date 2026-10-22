#ifndef MOCK_LISTVIEW_H
#define MOCK_LISTVIEW_H
#include <View.h>
#include <ListItem.h>
#include <Invoker.h>
#include <vector>
class BListView : public BView, public BInvoker {
public:
    BListView(const char* name, int type = 0) : BView(name, B_WILL_DRAW) { (void)type; }
    bool AddItem(BListItem* i) { fItems.push_back(i); return true; }
    BListItem* RemoveItem(int32 index) {
        if (index < 0 || (size_t)index >= fItems.size()) return 0;
        BListItem* it = fItems[(size_t)index];
        fItems.erase(fItems.begin()+index); return it; }
    bool RemoveItem(BListItem*) { return true; }
    BListItem* ItemAt(int32 i) const {
        return (i>=0 && (size_t)i<fItems.size()) ? fItems[(size_t)i] : 0; }
    int32 CountItems() const { return (int32)fItems.size(); }
    void MakeEmpty() { for (size_t i=0;i<fItems.size();i++) delete fItems[i]; fItems.clear(); }
    int32 CurrentSelection(int32 = 0) const { return fItems.empty() ? -1 : 0; }
    void Select(int32, bool = false) {}
    void DeselectAll() {}
    void ScrollToSelection() {}
    void SetSelectionMessage(BMessage* m) { delete m; }
    void SetInvocationMessage(BMessage* m) { delete m; }
    void SetEnabled(bool) {}
    void InvalidateItem(int32) {}
private:
    std::vector<BListItem*> fItems;
};
class BOutlineListView : public BListView {
public:
    BOutlineListView(const char* name, int type = 0) : BListView(name, type) {}
};
#endif
