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
        pounceT_ = -1.0f;
        pounceCd_ = 0.0f;
        rearUp_ = 0.0f;
        pounceAir_ = 0.0f;
        bodyStretch_ = 1.0f;
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
            // chase the butterfly: leopard run, then rear up and pounce
            butterflyPhase_ += dt;
            butterflyX_ = 70.0f + 180.0f * (0.5f + 0.5f * sinf(butterflyPhase_ * 0.8f));
            butterflyY_ = 48.0f + 22.0f * sinf(butterflyPhase_ * 1.6f);
            const float dx = butterflyX_ - walkX_;
            facingLeft_ = dx < 0.0f;
            walkPhase_ += 9.0f * dt;  // gallop stride ~1.4 Hz

            if (pounceCd_ > 0.0f) {
                pounceCd_ -= dt;
            }

            if (pounceT_ >= 0.0f) {
                // rear up on the hind legs, then leap
                pounceT_ += dt;
                constexpr float REAR_DUR = 0.28f;
                constexpr float LEAP_DUR = 0.42f;
                if (pounceT_ < REAR_DUR) {
                    rearUp_ = pounceT_ / REAR_DUR;
                    bob_ = rearUp_ * 3.0f;
                } else if (pounceT_ < REAR_DUR + LEAP_DUR) {
                    const float p = (pounceT_ - REAR_DUR) / LEAP_DUR;
                    rearUp_ = 0.0f;   // fold down into the leap
                    pounceAir_ = p;
                    bob_ = sinf(p * PI) * 17.0f;
                    walkX_ += (dx > 0.0f ? 1.0f : -1.0f) * 55.0f * dt;  // lunge
                } else {
                    pounceT_ = -1.0f;
                    rearUp_ = 0.0f;
                    pounceAir_ = 0.0f;
                    bob_ = 0.3f;
                    pounceCd_ = 0.5f;
                }
            } else if (fabsf(dx) <= 34.0f && pounceCd_ <= 0.0f) {
                pounceT_ = 0.0f;  // in range: stand up and pounce
                bodyStretch_ = 1.0f;
            } else {
                // leopard gallop: one back flex per stride
                bodyStretch_ = 0.5f + 0.5f * sinf(walkPhase_);
                walkX_ += (dx > 0.0f ? 1.0f : -1.0f) * 52.0f * dt;
                bob_ = 0.3f;
            }
            if (walkX_ < 40.0f) walkX_ = 40.0f;
            if (walkX_ > 280.0f) walkX_ = 280.0f;
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
    const int16_t cx = static_cast<int16_t>(walkX_);
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t ground = 110 - jump;

    const bool offline = state_ == PetState::OFFLINE;
    canvas_.fillEllipse(cx, 112, 40, 5, offline ? rgb565(50, 50, 55) : rgb565(58, 58, 66));

    blitSprite(cx, ground);

    if (state_ == PetState::WORKING) {
        drawButterfly2(static_cast<int16_t>(butterflyX_), static_cast<int16_t>(butterflyY_), sinf(t * 18.0f));
    } else if (state_ == PetState::COMPLETED) {
        drawButterfly2(cx + 24 * (facingLeft_ ? -1 : 1), ground - 40, 1.0f);
    }

    drawSymbols2(cx, ground - kCatSpriteH - 8, t);

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

uint16_t PetAnimation::desaturate(uint16_t c) {
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    uint32_t lum = (r * 2 + g + b) / 4;
    return (uint16_t)((lum << 11) | (lum << 5) | lum);
}

uint16_t PetAnimation::darken(uint16_t c) {
    return (uint16_t)((c >> 1) & 0x7BEF);
}

void PetAnimation::blitSprite(int16_t cx, int16_t groundY) {
    const bool offline = state_ == PetState::OFFLINE;
    const bool sleeping = state_ == PetState::SLEEP;
    const float sq = sleeping ? 0.72f : 1.0f;
    const int H = static_cast<int>(kCatSpriteH * sq);
    const int topY = groundY - H;
    for (int dy = 0; dy < H; ++dy) {
        const int sy = static_cast<int>(dy * kCatSpriteH / (float)H);
        for (int dx = 0; dx < kCatSpriteW; ++dx) {
            const int sx = facingLeft_ ? (kCatSpriteW - 1 - dx) : dx;
            const int idx = sy * kCatSpriteW + sx;
            if (!(kCatMask[idx >> 3] & (0x80 >> (idx & 7)))) continue;
            uint16_t c = kCatSprite[idx];
            if (offline) c = desaturate(c);
            else if (sleeping) c = darken(c);
            canvas_.drawPixel(cx - kCatSpriteW / 2 + dx, topY + dy, c);
        }
    }
}

void PetAnimation::drawButterfly2(int16_t bx, int16_t by, float flap) {
    const int16_t wing = 6 + static_cast<int16_t>(flap * 2.0f);
    const uint16_t wingColor = rgb565(255, 190, 90);
    const uint16_t wingDark = rgb565(140, 100, 220);
    canvas_.fillTriangle(bx, by, bx - wing, by - 6, bx - 3, by + 2, wingColor);
    canvas_.fillTriangle(bx, by, bx + wing, by - 6, bx + 3, by + 2, wingColor);
    canvas_.fillTriangle(bx, by, bx - wing + 3, by + 6, bx - 2, by + 2, wingDark);
    canvas_.fillTriangle(bx, by, bx + wing - 3, by + 6, bx + 2, by + 2, wingDark);
    canvas_.fillRect(bx - 1, by - 4, 2, 8, COLOR_LINE);
    canvas_.drawLine(bx, by - 4, bx - 3, by - 8, COLOR_LINE);
    canvas_.drawLine(bx, by - 4, bx + 3, by - 8, COLOR_LINE);
}

void PetAnimation::drawSymbols2(int16_t cx, int16_t cy, float t) {
    canvas_.setTextSize(2);
    canvas_.setTextColor(COLOR_WHITE);
    if (showQuestion_) { canvas_.setCursor(cx - 8, cy); canvas_.print('?'); }
    else if (showDots_) { canvas_.setCursor(cx - 22, cy); canvas_.print("..."); }
    else if (showStar_) {
        const float tw = 0.5f + 0.5f * sinf(t * 6.0f);
        const uint16_t starColor = (tw > 0.5f) ? COLOR_STAR : rgb565(200, 160, 40);
        for (int i = -1; i <= 1; ++i) {
            const int16_t sx = cx + i * 22, sy = cy + 4 + ((i % 2) ? 10 : 0);
            canvas_.fillRect(sx - 2, sy - 6, 5, 12, starColor);
            canvas_.fillRect(sx - 6, sy - 2, 12, 5, starColor);
        }
    }
    else if (showSweat_) {
        canvas_.fillCircle(cx + 34, cy + 6, 4, COLOR_TEAL);
        canvas_.fillCircle(cx + 33, cy + 5, 2, COLOR_BG);
    }
    if (showZzz_) {
        canvas_.setTextColor(COLOR_TEAL);
        canvas_.setTextSize(1);
        const int off = static_cast<int>(t * 12.0f) % 24;
        canvas_.setCursor(cx + 18, cy + 14 - off); canvas_.print('z');
        canvas_.setCursor(cx + 28, cy + 8 - off); canvas_.print('z');
        canvas_.setCursor(cx + 38, cy + 2 - off); canvas_.print('Z');
    }
}
