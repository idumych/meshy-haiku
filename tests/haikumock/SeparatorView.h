#ifndef MOCK_SEPARATORVIEW_H
#define MOCK_SEPARATORVIEW_H
#include <View.h>
class BSeparatorView : public BView {
public:
    BSeparatorView(orientation o) : BView("sep", 0) { (void)o; }
    BSeparatorView(const char* n, orientation o) : BView(n, 0) { (void)o; }
};
#endif
