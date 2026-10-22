#ifndef MOCK_APPLICATION_H
#define MOCK_APPLICATION_H
#include <Looper.h>
class BApplication : public BLooper {
public:
    BApplication(const char* sig) : BLooper(sig) {}
    virtual ~BApplication() {}
    virtual void ReadyToRun() {}
    virtual void AboutRequested() {}
    virtual bool QuitRequested() { return true; }
    virtual void MessageReceived(BMessage*) {}
    void Run() {}
};
extern BApplication* be_app;
#endif
