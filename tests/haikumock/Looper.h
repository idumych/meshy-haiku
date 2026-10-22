#ifndef MOCK_LOOPER_H
#define MOCK_LOOPER_H
#include <Handler.h>
class BLooper : public BHandler {
public:
    BLooper(const char* name = 0) : BHandler(name) {}
    status_t PostMessage(uint32) { return B_OK; }
    status_t PostMessage(BMessage*) { return B_OK; }
    bool Lock() { return true; }
    void Unlock() {}
};
#endif
