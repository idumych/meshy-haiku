#ifndef MOCK_FINDDIRECTORY_H
#define MOCK_FINDDIRECTORY_H
#include <SupportDefs.h>
enum directory_which { B_USER_SETTINGS_DIRECTORY, B_USER_DIRECTORY };
class BPath;
status_t find_directory(directory_which, BPath*);
#endif
