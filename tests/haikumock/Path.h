#ifndef MOCK_PATH_H
#define MOCK_PATH_H
#include <SupportDefs.h>
#include <String.h>
class BPath {
public:
    BPath() {}
    const char* Path() const { return fPath.String(); }
    status_t Append(const char* p) { fPath << "/" << p; return B_OK; }
private:
    BString fPath;
};
#endif
