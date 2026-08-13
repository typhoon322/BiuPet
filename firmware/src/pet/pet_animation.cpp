#include "pet_animation.h"

#include <Arduino.h>
#include <math.h>

namespace {

const float TAU = 6.2831853f;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COLOR_BG       = 0x0000;
const uint16_t COLOR_WHITE    = 0xFFFF;
const uint16_t COLOR_BODY     = rgb565(246, 200, 128);   // warm orange tabby
const uint16_t COLOR_BODY_DK  = rgb565(214, 156, 88);
const uint16_t COLOR_BELLY    = rgb565(252, 226, 180);
const uint16_t COLOR_EAR_IN   = rgb565(242, 158, 172);
const uint16_t COLOR_LINE     = rgb565(64, 42, 28);
const uint16_t COLOR_EYE      = rgb565(38, 38, 46);
const uint16_t COLOR_EYE_LIT  = 0xFFFF;
const uint16_t COLOR_BLUSH    = rgb565(252, 152, 164);
const uint16_t COLOR_GRAY     = rgb565(148, 148, 154);
const uint16_t COLOR_GRAY_DK  = rgb565(102, 102, 110);
const uint16_t COLOR_GRAY_BL  = rgb565(178, 178, 184);
const uint16_t COLOR_RED      = rgb565(235, 82, 82);
const uint16_t COLOR_STAR     = rgb565(255, 214, 84);
const uint16_t COLOR_TEAL     = rgb565(104, 220, 210);

void fillRoundRectAuto(GFXcanvas16& c, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    c.fillRoundRect(x, y, w, h, r, color);
}

} // namespace

PetAnimation::PetAnimation() = default;

void PetAnimation::begin() {
    stateStartedMs_ = millis();
    lastFrameMs_ = millis();
}

void PetAnimation::setState(PetState state) {
    if (state_ != state) {
        state_ = state;
        stateStartedMs_ = millis();
        frame_ = 0;
        // reset pose defaults
        bob_ = 0.0f;
        squash_ = 0.0f;
        headTiltX_ = 0.0f;
        headTiltY_ = 0.0f;
        earDropL_ = 0.0f;
        earDropR_ = 0.0f;
        tailAngle_ = 0.3f;
        pawLiftL_ = 0.0f;
        pawLiftR_ = 0.0f;
        eyesOpen_ = 1.0f;
        eyeX_ = 0.0f;
        eyeStyle_ = 0;
        mouthStyle_ = 0;
        showZzz_ = false;
        showQuestion_ = false;
        showDots_ = false;
        showStar_ = false;
        showSweat_ = false;
    }
}

void PetAnimation::update(uint32_t nowMs) {
    const float t = static_cast<float>(nowMs - stateStartedMs_) / 1000.0f;
    const float phase = t * TAU;
    const float dt = fminf((nowMs - lastFrameMs_) / 1000.0f, 0.25f);
    lastFrameMs_ = nowMs;
    frame_++;

    // clear per-frame state symbols
    showZzz_ = false;
    showQuestion_ = false;
    showDots_ = false;
    showStar_ = false;
    showSweat_ = false;
    eyesOpen_ = 1.0f;
    eyeStyle_ = 0;
    mouthStyle_ = 0;
    headTiltX_ = 0.0f;
    headTiltY_ = 0.0f;
    squash_ = 0.0f;

    switch (state_) {
        case PetState::IDLE: {
            // leisurely catwalk: slow steps, diagonal gait
            facingLeft_ = walkDir_ < 0.0f;
            walkPhase_ += 4.6f * dt;              // ~0.73 Hz step cycle
            walkX_ += walkDir_ * 26.0f * dt;      // ~26 px/s
            if (walkX_ > 280.0f) {
                walkX_ = 280.0f;
                walkDir_ = -1.0f;
            }
            if (walkX_ < 40.0f) {
                walkX_ = 40.0f;
                walkDir_ = 1.0f;
            }
            bob_ = sinf(walkPhase_) * 0.35f;      // gentle body bob
            break;
        }
        case PetState::WORKING: {
            // chase the butterfly
            butterflyPhase_ += dt;
            butterflyX_ = 70.0f + 180.0f * (0.5f + 0.5f * sinf(butterflyPhase_ * 0.8f));
            butterflyY_ = 48.0f + 22.0f * sinf(butterflyPhase_ * 1.6f);
            const float dx = butterflyX_ - walkX_;
            if (jumpT_ >= 0.0f) {
                // airborne: parabolic jump toward the butterfly
                jumpT_ += dt;
                if (jumpT_ >= 0.45f) {
                    jumpT_ = -1.0f;
                    bob_ = 0.3f;
                } else {
                    const float p = jumpT_ / 0.45f;
                    bob_ = sinf(p * PI) * 18.0f;
                }
                walkX_ += (dx > 0.0f ? 1.0f : -1.0f) * 18.0f * dt;  // lunge
            } else if (fabsf(dx) <= 26.0f) {
                jumpT_ = 0.0f;  // in range -> pounce!
                bob_ = 0.3f;
            } else {
                walkX_ += (dx > 0.0f ? 1.0f : -1.0f) * 46.0f * dt;
                bob_ = 0.3f;
            }
            if (walkX_ < 40.0f) walkX_ = 40.0f;
            if (walkX_ > 280.0f) walkX_ = 280.0f;
            facingLeft_ = dx < 0.0f;
            walkPhase_ += 16.0f * dt;
            eyesOpen_ = 0.7f + 0.3f * sinf(phase * 3.0f);
            mouthStyle_ = 1;
            headTiltY_ = 1.0f;
            break;
        }
        case PetState::WAITING: {
            walkPhase_ += 2.0f * dt;
            bob_ = sinf(phase * 1.1f) * 0.5f;
            headTiltY_ = 3.0f;
            showQuestion_ = true;
            break;
        }
        case PetState::COMPLETED: {
            bob_ = fabsf(sinf(phase * 3.2f)) * 8.0f;   // happy hops, holding the prize
            eyeStyle_ = 1;                         // happy
            walkPhase_ += 6.0f * dt;
            showStar_ = true;
            break;
        }
        case PetState::ERROR: {
            eyeStyle_ = 2;                         // X
            mouthStyle_ = 2;                       // frown
            headTiltY_ = 4.0f;
            bob_ = sinf(phase * 8.0f) * 0.7f;      // shake
            showSweat_ = true;
            break;
        }
        case PetState::SLEEP: {
            squash_ = 0.5f;                        // curled up
            eyeStyle_ = 3;                         // closed
            bob_ = sinf(phase * 0.5f) * 0.6f;
            showZzz_ = true;
            break;
        }
        case PetState::OFFLINE: {
            eyeStyle_ = 4;                         // dull dot
            bob_ = sinf(phase * 0.4f) * 0.4f;
            break;
        }
    }
}

void PetAnimation::draw(Adafruit_ST7789& tft, int16_t x, int16_t y) {
    canvas_.fillScreen(COLOR_BG);

    const float t = static_cast<float>(millis() - stateStartedMs_) / 1000.0f;
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t m = facingLeft_ ? -1 : 1;
    const int16_t cx = static_cast<int16_t>(walkX_);
    const int16_t ground = 110 - jump;

    uint16_t bodyColor = COLOR_BODY;
    uint16_t darkColor = COLOR_BODY_DK;
    uint16_t bellyColor = COLOR_BELLY;
    uint16_t lineColor = COLOR_LINE;
    if (state_ == PetState::OFFLINE) {
        bodyColor = COLOR_GRAY;
        darkColor = COLOR_GRAY_DK;
        bellyColor = COLOR_GRAY_BL;
        lineColor = COLOR_GRAY_DK;
    } else if (state_ == PetState::ERROR) {
        bodyColor = rgb565(240, 186, 150);
    }

    // ground shadow
    canvas_.fillEllipse(cx, 114, 34, 4, rgb565(58, 58, 66));

    // tail behind the body
    drawSideTail(cx, ground, m, t, darkColor);

    // far legs (darker, behind the body)
    drawSideLeg(cx - 16 * m, ground, m, 2, darkColor, 0.0f, t);     // far back (with near front)
    drawSideLeg(cx + 5 * m, ground, m, 2, darkColor, PI, t);        // far front (with near back)

    // body
    const int16_t bodyW = (state_ == PetState::SLEEP) ? 62 : 44;
    const int16_t bodyH = (state_ == PetState::SLEEP) ? 20 : 26;
    const int16_t bodyCy = ground - 10 - bodyH / 2;  // bottom floats 10px above ground
    fillRoundRectAuto(canvas_, cx - bodyW / 2, bodyCy - bodyH / 2, bodyW, bodyH, 14, bodyColor);
    // belly
    fillRoundRectAuto(canvas_, cx - bodyW / 2 + 4, bodyCy + 2, bodyW - 8, bodyH / 2 - 2, 10, bellyColor);
    // back stripes
    if (state_ != PetState::OFFLINE) {
        for (int i = 0; i < 3; ++i) {
            canvas_.fillRoundRect(cx - bodyW / 2 + 9 + i * 13, bodyCy - bodyH / 2 - 1, 5, 9, 2, darkColor);
        }
    }

    // near legs (drawn over the body so they look connected)
    drawSideLeg(cx - 12 * m, ground, m, 1, bodyColor, PI, t);       // near back
    drawSideLeg(cx + 10 * m, ground, m, 1, bodyColor, 0.0f, t);     // near front

    // head + face
    drawSideHead(cx, ground, m, t, bodyColor, darkColor, bellyColor, lineColor);

    // butterfly: flying while chasing, held up when caught
    if (state_ == PetState::WORKING) {
        drawButterfly(static_cast<int16_t>(butterflyX_), static_cast<int16_t>(butterflyY_),
                      sinf(t * 18.0f));
    } else if (state_ == PetState::COMPLETED) {
        drawButterfly(cx + 16 * m, ground - 26, 1.0f);  // presented in the raised paw
    }

    // state symbols above the head
    drawSymbols(cx + 24 * m, ground - 62, t);

    const uint16_t* buf = canvas_.getBuffer();
    tft.startWrite();
    tft.setAddrWindow(x, y, CANVAS_W, CANVAS_H);
    uint32_t total = CANVAS_W * CANVAS_H;
    uint32_t off = 0;
    while (off < total) {
        const uint32_t n = (total - off > 1024) ? 1024 : (total - off);
        tft.writePixels(const_cast<uint16_t*>(buf + off), n);
        off += n;
    }
    tft.endWrite();
}

void PetAnimation::drawButterfly(int16_t bx, int16_t by, float flap) {
    const int16_t wing = 6 + static_cast<int16_t>(flap * 2.0f);
    const uint16_t wingColor = rgb565(255, 190, 90);
    const uint16_t wingDark = rgb565(140, 100, 220);
    // upper wings
    canvas_.fillTriangle(bx, by, bx - wing, by - 6, bx - 3, by + 2, wingColor);
    canvas_.fillTriangle(bx, by, bx + wing, by - 6, bx + 3, by + 2, wingColor);
    // lower wings
    canvas_.fillTriangle(bx, by, bx - wing + 3, by + 6, bx - 2, by + 2, wingDark);
    canvas_.fillTriangle(bx, by, bx + wing - 3, by + 6, bx + 2, by + 2, wingDark);
    // body
    canvas_.fillRect(bx - 1, by - 4, 2, 8, COLOR_LINE);
    // antennae
    canvas_.drawLine(bx, by - 4, bx - 3, by - 8, COLOR_LINE);
    canvas_.drawLine(bx, by - 4, bx + 3, by - 8, COLOR_LINE);
}

void PetAnimation::drawSideTail(int16_t cx, int16_t ground, int16_t m, float t, uint16_t color) {
    // attach at the rump, not the rear foot
    const int16_t baseX = cx - 19 * m;
    const int16_t baseY = ground - 28;
    const int16_t tipX = baseX - 14 * m + static_cast<int16_t>(sinf(t * 2.0f + 1.0f) * 4.0f);
    const int16_t tipY = baseY - 14 + static_cast<int16_t>(sinf(t * 3.0f) * 3.0f);
    const int segments = 5;
    for (int i = segments; i >= 0; --i) {
        const float f = static_cast<float>(i) / segments;
        const int16_t px = baseX + static_cast<int16_t>((tipX - baseX) * f);
        const int16_t py = baseY + static_cast<int16_t>((tipY - baseY) * f);
        canvas_.fillCircle(px, py, 3 + static_cast<int16_t>(f * 1.5f), color);
    }
    canvas_.fillCircle(tipX, tipY, 3, (state_ == PetState::OFFLINE) ? COLOR_GRAY : COLOR_BODY);
}

void PetAnimation::drawSideLeg(int16_t lx, int16_t ground, int16_t m, int layer,
                               uint16_t color, float phase, float t) {
    (void)t;
    float lift = 0.0f;
    if (state_ == PetState::IDLE) {
        lift = fmaxf(sinf(walkPhase_ + phase), 0.0f);
        lx += static_cast<int16_t>(sinf(walkPhase_ + phase) * 2.0f) * m;
    } else if (state_ == PetState::WORKING) {
        lift = fmaxf(sinf(walkPhase_ * 1.5f + phase), 0.0f);
        if (bob_ > 6.0f) {
            lift = 1.0f;  // airborne: tuck the legs
        }
    } else if (state_ == PetState::COMPLETED) {
        lift = (layer == 1) ? 1.0f : 0.0f;
    }
    const int16_t liftPx = static_cast<int16_t>(lift * 3.0f);
    fillRoundRectAuto(canvas_, lx - 4, ground - 12 + liftPx, 8, 13 - liftPx, 4, color);
    if (layer == 1) {
        const uint16_t tick = (state_ == PetState::OFFLINE) ? COLOR_GRAY_DK : COLOR_BODY_DK;
        canvas_.drawLine(lx - 2, ground - 3, lx - 1, ground - 5, tick);
        canvas_.drawLine(lx + 1, ground - 3, lx + 2, ground - 5, tick);
    }
}

void PetAnimation::drawSideHead(int16_t cx, int16_t ground, int16_t m, float t,
                                uint16_t headColor, uint16_t darkColor,
                                uint16_t snoutColor, uint16_t lineColor) {
    (void)t;
    const int16_t hx = cx + 24 * m;
    const int16_t hy = ground - 46 + static_cast<int16_t>(16.0f * squash_);
    const int16_t r = 15;

    // ear (single, on the visible side)
    canvas_.fillTriangle(hx - 6 * m, hy - r + 4, hx - 3 * m, hy - r - 9,
                         hx + 6 * m, hy - r + 5, headColor);
    canvas_.fillTriangle(hx - 3 * m, hy - r + 5, hx - 2 * m, hy - r - 4,
                         hx + 3 * m, hy - r + 6, COLOR_EAR_IN);

    // head
    canvas_.fillCircle(hx, hy, r, headColor);

    // snout bump (front of the face)
    canvas_.fillCircle(hx + 9 * m, hy + 4, 7, snoutColor);

    // forehead stripes
    if (state_ != PetState::OFFLINE) {
        canvas_.fillRoundRect(hx - 5 * m, hy - r - 1, 5, 7, 2, darkColor);
        canvas_.fillRoundRect(hx + 3 * m, hy - r - 3, 5, 7, 2, darkColor);
    }

    // eye
    const int16_t ex = hx + 3 * m;
    const int16_t ey = hy - 3;
    if (eyeStyle_ == 1) {  // happy ^
        canvas_.drawLine(ex - 4, ey + 1, ex - 1, ey - 3, lineColor);
        canvas_.drawLine(ex + 1, ey - 3, ex + 4, ey + 1, lineColor);
    } else if (eyeStyle_ == 2) {  // X
        canvas_.drawLine(ex - 3, ey - 3, ex + 3, ey + 3, lineColor);
        canvas_.drawLine(ex - 3, ey + 3, ex + 3, ey - 3, lineColor);
    } else if (eyeStyle_ == 3 || eyesOpen_ < 0.35f) {  // closed
        canvas_.drawLine(ex - 4, ey, ex + 4, ey, lineColor);
    } else if (eyeStyle_ == 4) {  // dull dot
        canvas_.fillCircle(ex, ey, 2, darkColor);
    } else {
        canvas_.fillEllipse(ex, ey, 4, (static_cast<int16_t>(4 * eyesOpen_) > 1)
                                       ? static_cast<int16_t>(4 * eyesOpen_) : 1, lineColor);
        canvas_.fillCircle(ex + 1, ey - 1, 1, COLOR_EYE_LIT);
    }

    // nose
    canvas_.fillCircle(hx + 11 * m, hy + 3, 2, COLOR_EAR_IN);

    // mouth
    const int16_t my = hy + 5;
    if (mouthStyle_ == 0) {
        canvas_.drawLine(hx + 11 * m, my, hx + 7 * m, my + 4, lineColor);
    } else if (mouthStyle_ == 1) {
        canvas_.fillCircle(hx + 10 * m, my + 3, 3, COLOR_RED);
    } else if (mouthStyle_ == 2) {
        canvas_.drawLine(hx + 11 * m, my + 4, hx + 6 * m, my + 1, lineColor);
    } else if (mouthStyle_ == 3) {
        canvas_.fillCircle(hx + 10 * m, my + 2, 3, COLOR_RED);
    }

    // whiskers
    canvas_.drawLine(hx + 10 * m, hy - 2, hx + 23 * m, hy - 4, lineColor);
    canvas_.drawLine(hx + 10 * m, hy + 2, hx + 24 * m, hy + 5, lineColor);

    // blush
    canvas_.fillCircle(hx + 2 * m, hy + 7, 3, COLOR_BLUSH);
}

void PetAnimation::drawSymbols(int16_t cx, int16_t cy, float t) {
    canvas_.setTextSize(2);
    canvas_.setTextColor(COLOR_WHITE);
    if (showQuestion_) {
        canvas_.setCursor(cx - 8, cy);
        canvas_.print('?');
    } else if (showDots_) {
        canvas_.setCursor(cx - 22, cy);
        canvas_.print("...");
    } else if (showStar_) {
        // three small stars that twinkle
        const float tw = 0.5f + 0.5f * sinf(t * 6.0f);
        const uint16_t starColor = (tw > 0.5f) ? COLOR_STAR : rgb565(200, 160, 40);
        for (int i = -1; i <= 1; ++i) {
            const int16_t sx = cx + i * 22;
            const int16_t sy = cy + 4 + ((i % 2) ? 10 : 0);
            canvas_.fillRect(sx - 2, sy - 6, 5, 12, starColor);
            canvas_.fillRect(sx - 6, sy - 2, 12, 5, starColor);
        }
    } else if (showSweat_) {
        canvas_.fillCircle(cx + 34, cy + 6, 4, COLOR_TEAL);
        canvas_.fillCircle(cx + 33, cy + 5, 2, COLOR_BG);
    }

    if (showZzz_) {
        canvas_.setTextColor(COLOR_TEAL);
        canvas_.setTextSize(1);
        const int off = static_cast<int>(t * 12.0f) % 24;
        canvas_.setCursor(cx + 18, cy + 14 - off);
        canvas_.print('z');
        canvas_.setCursor(cx + 28, cy + 8 - off);
        canvas_.print('z');
        canvas_.setCursor(cx + 38, cy + 2 - off);
        canvas_.print('Z');
    }
}
