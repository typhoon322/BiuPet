#pragma once

#include <Adafruit_ST7789.h>
#include "pet_state.h"

class PetAnimation {
public:
    PetAnimation();
    void begin();
    void setState(PetState state);
    PetState state() const { return state_; }
    void update(uint32_t nowMs);
    void draw(Adafruit_ST7789& tft, int16_t x, int16_t y);

private:
    // full-screen canvas: cat walks across the whole 320px display
    static constexpr int16_t SIZE = 128;
    static constexpr int16_t CANVAS_W = 320;
    static constexpr int16_t CANVAS_H = 128;

    PetState state_ = PetState::IDLE;
    GFXcanvas16 canvas_{CANVAS_W, CANVAS_H};
    uint32_t stateStartedMs_ = 0;
    uint32_t lastFrameMs_ = 0;
    int32_t frame_ = 0;

    // pose parameters computed in update(), consumed in draw()
    float bob_ = 0.0f;          // vertical hop / breathing (pixels, can be >1 for jump)
    float squash_ = 0.0f;       // 0..1 body squish (sleep)
    float headTiltX_ = 0.0f;
    float headTiltY_ = 0.0f;
    float earDropL_ = 0.0f;
    float earDropR_ = 0.0f;
    float tailAngle_ = 0.0f;    // radians, 0 = out to the right
    float pawLiftL_ = 0.0f;     // 0..1 lift amount for left front paw
    float pawLiftR_ = 0.0f;
    float eyesOpen_ = 1.0f;
    float eyeX_ = 0.0f;
    int eyeStyle_ = 0;          // 0 round, 1 happy, 2 X, 3 sleep dash, 4 dot
    int mouthStyle_ = 0;        // 0 smile, 1 open, 2 frown, 3 O
    bool showZzz_ = false;
    bool showQuestion_ = false;
    bool showDots_ = false;
    bool showStar_ = false;
    bool showSweat_ = false;

    // side-profile walking cat (option A)
    float walkX_ = 160.0f;      // body center x in display space (paces 40..280)
    float walkDir_ = 1.0f;      // +1 right, -1 left
    float walkPhase_ = 0.0f;    // leg cycle phase (radians)
    bool facingLeft_ = false;   // mirror flag for the head/legs
    float butterflyX_ = 240.0f; // WORKING: butterfly position
    float butterflyY_ = 46.0f;
    float butterflyPhase_ = 0.0f;

    void drawSideHead(int16_t cx, int16_t ground, int16_t m, float t,
                      uint16_t headColor, uint16_t darkColor,
                      uint16_t snoutColor, uint16_t lineColor);
    void drawSideLeg(int16_t lx, int16_t ground, int16_t m, int layer,
                     uint16_t color, float t);
    void drawSideTail(int16_t cx, int16_t ground, int16_t m, float t, uint16_t color);
    void drawButterfly(float t);
    void drawSymbols(int16_t cx, int16_t cy, float t);
};
