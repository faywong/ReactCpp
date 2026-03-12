#pragma once

#include <cstddef>

#if __has_include(<yoga/Yoga.h>)
#include <yoga/Yoga.h>
#else
extern "C" {

typedef struct YGNode* YGNodeRef;
typedef const struct YGNode* YGNodeConstRef;

typedef enum YGFlexDirection {
    YGFlexDirectionColumn = 0,
    YGFlexDirectionColumnReverse = 1,
    YGFlexDirectionRow = 2,
    YGFlexDirectionRowReverse = 3,
} YGFlexDirection;

typedef enum YGJustify {
    YGJustifyFlexStart = 0,
    YGJustifyCenter = 1,
    YGJustifyFlexEnd = 2,
    YGJustifySpaceBetween = 3,
    YGJustifySpaceAround = 4,
    YGJustifySpaceEvenly = 5,
} YGJustify;

typedef enum YGAlign {
    YGAlignAuto = 0,
    YGAlignFlexStart = 1,
    YGAlignCenter = 2,
    YGAlignFlexEnd = 3,
    YGAlignStretch = 4,
    YGAlignBaseline = 5,
    YGAlignSpaceBetween = 6,
    YGAlignSpaceAround = 7,
    YGAlignSpaceEvenly = 8,
} YGAlign;

typedef enum YGDirection {
    YGDirectionInherit = 0,
    YGDirectionLTR = 1,
    YGDirectionRTL = 2,
} YGDirection;

typedef enum YGEdge {
    YGEdgeLeft = 0,
    YGEdgeTop = 1,
    YGEdgeRight = 2,
    YGEdgeBottom = 3,
    YGEdgeStart = 4,
    YGEdgeEnd = 5,
    YGEdgeHorizontal = 6,
    YGEdgeVertical = 7,
    YGEdgeAll = 8,
} YGEdge;

typedef enum YGMeasureMode {
    YGMeasureModeUndefined = 0,
    YGMeasureModeExactly = 1,
    YGMeasureModeAtMost = 2,
} YGMeasureMode;

typedef struct YGSize {
    float width;
    float height;
} YGSize;

typedef YGSize (*YGMeasureFunc)(
    YGNodeConstRef node,
    float width,
    YGMeasureMode widthMode,
    float height,
    YGMeasureMode heightMode);

YGNodeRef YGNodeNew(void);
void YGNodeFree(YGNodeRef node);
void YGNodeInsertChild(YGNodeRef node, YGNodeRef child, size_t index);
void YGNodeRemoveAllChildren(YGNodeRef node);
void YGNodeCalculateLayout(YGNodeRef node, float availableWidth, float availableHeight, YGDirection ownerDirection);
void YGNodeSetContext(YGNodeRef node, void* context);
void* YGNodeGetContext(YGNodeConstRef node);
void YGNodeSetMeasureFunc(YGNodeRef node, YGMeasureFunc measureFunc);

void YGNodeStyleSetFlexDirection(YGNodeRef node, YGFlexDirection flexDirection);
void YGNodeStyleSetJustifyContent(YGNodeRef node, YGJustify justifyContent);
void YGNodeStyleSetAlignItems(YGNodeRef node, YGAlign alignItems);
void YGNodeStyleSetFlexGrow(YGNodeRef node, float flexGrow);
void YGNodeStyleSetFlexShrink(YGNodeRef node, float flexShrink);
void YGNodeStyleSetPadding(YGNodeRef node, YGEdge edge, float padding);
void YGNodeStyleSetMargin(YGNodeRef node, YGEdge edge, float margin);
void YGNodeStyleSetWidth(YGNodeRef node, float width);
void YGNodeStyleSetWidthAuto(YGNodeRef node);
void YGNodeStyleSetHeight(YGNodeRef node, float height);
void YGNodeStyleSetHeightAuto(YGNodeRef node);

float YGNodeLayoutGetLeft(YGNodeConstRef node);
float YGNodeLayoutGetTop(YGNodeConstRef node);
float YGNodeLayoutGetWidth(YGNodeConstRef node);
float YGNodeLayoutGetHeight(YGNodeConstRef node);

}
#endif
