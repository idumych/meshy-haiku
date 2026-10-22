#ifndef MOCK_RECT_H
#define MOCK_RECT_H
#include <SupportDefs.h>
class BPoint { public: float x, y; BPoint():x(0),y(0){} BPoint(float a,float b):x(a),y(b){} };
class BRect {
public:
    float left, top, right, bottom;
    BRect():left(0),top(0),right(0),bottom(0){}
    BRect(float l,float t,float r,float b):left(l),top(t),right(r),bottom(b){}
    float Width() const { return right-left; }
    float Height() const { return bottom-top; }
    bool Contains(BPoint p) const { return p.x>=left&&p.x<=right&&p.y>=top&&p.y<=bottom; }
};
class BSize { public: float width, height; BSize():width(0),height(0){} BSize(float w,float h):width(w),height(h){} };
#define B_SIZE_UNSET (-2.0f)
#define B_SIZE_UNLIMITED (1e9f)
class BAlignment { public: BAlignment(int,int){} };
#define B_ALIGN_LEFT 0
#define B_ALIGN_RIGHT 1
#define B_ALIGN_CENTER 2
#define B_ALIGN_VERTICAL_CENTER 3
#endif
