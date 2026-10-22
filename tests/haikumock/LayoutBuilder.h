#ifndef MOCK_LAYOUTBUILDER_H
#define MOCK_LAYOUTBUILDER_H
// Mirrors only the chaining surface this project uses.
#include <View.h>
#include <Window.h>
#include <GroupLayout.h>
#include <GridLayout.h>
namespace BLayoutBuilder {
template <typename T = void>
class Group {
public:
    Group(BView*, orientation, float = 0, float = 0) {}
    Group(BWindow*, orientation, float = 0, float = 0) {}
    Group(orientation, float = 0, float = 0) {}
    Group& SetInsets(float) { return *this; }
    Group& SetInsets(float, float, float, float) { return *this; }
    Group& Add(BView*) { return *this; }
    Group& Add(BView*, float) { return *this; }
    Group& Add(BLayout*) { return *this; }
    Group& AddGroup(orientation, float = 0, float = 0) { return *this; }
    Group& AddGlue(float = 1.0f) { return *this; }
    Group& AddStrut(float) { return *this; }
    Group& End() { return *this; }
};
template <typename T = void>
class Grid {
public:
    Grid(BView*, float = 0, float = 0) {}
    Grid& Add(BView*, int32, int32, int32 = 1, int32 = 1) { return *this; }
    Grid& End() { return *this; }
};
}  // namespace BLayoutBuilder
#endif
