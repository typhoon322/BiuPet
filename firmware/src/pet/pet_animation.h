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
    int externalFrameCount(PetState state);
    void refreshExternalFrames();
    void draw(Adafruit_ST7789& tft, int16_t x, int16_t y);

private:
    static constexpr int16_t SIZE = 128;

    PetState state_ = PetState::IDLE;
    GFXcanvas16 canvas_{SIZE, SIZE};
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
    uint16_t* extBuf_ = nullptr;
    int extCount_[7] = {-1, -1, -1, -1, -1, -1, -1};
    uint32_t extCountAtMs_[7] = {0, 0, 0, 0, 0, 0, 0};
    uint32_t extDelayMs_[7] = {0, 0, 0, 0, 0, 0, 0};
    int extFrame_ = 0;
    uint32_t lastExtFrameMs_ = 0;

    void drawFace();
    void drawBody();
    void drawTail();
    void drawPaws();
    void drawSymbols(int16_t cx, int16_t cy, float t);
};
