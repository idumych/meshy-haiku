#ifndef MOCK_GRAPHICSDEFS_H
#define MOCK_GRAPHICSDEFS_H
#include <SupportDefs.h>
struct rgb_color { uint8 red, green, blue, alpha; };
#define B_SOLID_LOW 1
#define B_SOLID_HIGH 2
#define B_TRANSPARENT_COLOR (rgb_color{0,0,0,0})
#endif
