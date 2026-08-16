#pragma once

#include "display/display_config.h"
#include "pet_state.h"
#include "ui/cat_frames.h"

class PetAnimation {
public:
    PetAnimation();
    void begin();
    void setState(PetState state);
    PetState state() const { return state_; }
    void update(uint32_t nowMs);
    void draw(LGFX& tft, int16_t x, int16_t y);

private:
    // full-screen canvas: cat walks across the whole 320px display
    static constexpr int16_t SIZE = 128;
    static constexpr int16_t CANVAS_W = 320;
    static constexpr int16_t CANVAS_H = 130;

    PetState state_ = PetState::IDLE;
    LGFX_Sprite canvas_;
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
    bool caught_ = false;       // WORKING: butterfly is caught (held at the mouth)

    // side-profile walking cat (option A)
    float walkX_ = 160.0f;      // body center x in display space (paces 40..280)
    float walkDir_ = 1.0f;      // +1 right, -1 left
    float walkPhase_ = 0.0f;    // leg cycle phase (radians)
    bool facingLeft_ = false;   // mirror flag for the head/legs
    float butterflyX_ = 240.0f; // WORKING: butterfly position
    float butterflyY_ = 46.0f;
    float butterflyPhase_ = 0.0f;
    float pounceT_ = -1.0f;     // WORKING pounce: >=0 in progress
    float pounceCd_ = 0.0f;     // cooldown before the next pounce
    float rearUp_ = 0.0f;       // 0..1: standing up, front paws raised
    float pounceAir_ = 0.0f;    // 0..1: airborne stretch of the leap
    float bodyStretch_ = 1.0f;  // chase: spine stretch for the leopard run

    void blitFrame(int frame, int16_t cx, int16_t groundY);
    void animInfo(int& base, int& count, int& fps) const;
    static uint16_t desaturate(uint16_t c);
    static uint16_t darken(uint16_t c);
    void drawButterfly2(int16_t bx, int16_t by, float flap);
    void drawSymbols2(int16_t cx, int16_t cy, float t);
};
