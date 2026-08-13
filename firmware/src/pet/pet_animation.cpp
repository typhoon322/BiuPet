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

void drawEye(GFXcanvas16& c, int16_t cx, int16_t cy, int style, float open, float lookX) {
    switch (style) {
        case 1: { // happy ^ ^
            c.drawLine(cx - 7, cy - 1, cx - 2, cy + 4, COLOR_LINE);
            c.drawLine(cx + 2, cy + 4, cx + 7, cy - 1, COLOR_LINE);
            break;
        }
        case 2: { // X
            c.drawLine(cx - 6, cy - 6, cx + 6, cy + 6, COLOR_LINE);
            c.drawLine(cx - 6, cy + 6, cx + 6, cy - 6, COLOR_LINE);
            break;
        }
        case 3: { // sleep dash
            c.drawLine(cx - 6, cy, cx + 6, cy, COLOR_LINE);
            break;
        }
        case 4: { // small dull dot
            c.fillCircle(cx, cy, 2, COLOR_EYE);
            break;
        }
        default: { // big round eye with highlight
            const int16_t r = 7;
            int16_t h = static_cast<int16_t>(r * open);
            if (h <= 1) {
                c.drawLine(cx - r, cy, cx + r, cy, COLOR_LINE);
                break;
            }
            const int16_t ox = static_cast<int16_t>(lookX * 2.0f);
            c.fillRect(cx - r, cy - h, r * 2, h * 2 + 1, COLOR_EYE);
            c.fillCircle(cx + ox, cy, 2, COLOR_EYE_LIT);
            c.fillCircle(cx + ox - 2, cy - 2, 1, COLOR_EYE_LIT);
            break;
        }
    }
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
    frame_++;

    switch (state_) {
        case PetState::IDLE: {
            const float blink = fmodf(t, 3.8f);
            eyesOpen_ = (blink > 3.2f) ? (1.0f - (blink - 3.2f) / 0.6f) : 1.0f;
            if (eyesOpen_ < 0.0f) eyesOpen_ = 0.0f;
            eyeStyle_ = (eyesOpen_ < 0.35f) ? 3 : 0;
            mouthStyle_ = 0;
            bob_ = sinf(phase * 0.6f) * 0.5f;      // gentle breathing
            tailAngle_ = 0.3f + sinf(phase * 1.2f) * 0.35f;
            squash_ = 0.0f;
            break;
        }
        case PetState::WORKING: {
            eyeX_ = sinf(phase * 2.6f) * 0.9f;
            eyesOpen_ = 0.7f + 0.3f * sinf(phase * 3.0f);
            if (eyesOpen_ < 0.35f) eyesOpen_ = 0.35f;
            eyeStyle_ = 0;
            mouthStyle_ = 1;                       // typing mouth
            bob_ = sinf(phase * 6.0f) * 0.7f;      // busy bounce
            // alternating typing paws
            pawLiftL_ = (fmodf(t, 0.9f) < 0.45f) ? 1.0f : 0.0f;
            pawLiftR_ = (fmodf(t, 0.9f) >= 0.45f) ? 1.0f : 0.0f;
            headTiltX_ = 0.0f;
            headTiltY_ = 2.0f;                     // focused, slightly down
            tailAngle_ = 0.1f + sinf(phase * 4.0f) * 0.2f;
            showDots_ = true;
            squash_ = 0.0f;
            break;
        }
        case PetState::WAITING: {
            eyesOpen_ = 1.0f;
            eyeStyle_ = 0;
            eyeX_ = 0.0f;
            mouthStyle_ = 3;                       // O
            headTiltX_ = 4.0f + sinf(phase * 0.8f) * 1.5f; // tilted head
            headTiltY_ = 1.0f;
            bob_ = sinf(phase * 1.1f) * 0.6f;
            tailAngle_ = -0.2f + sinf(phase * 0.9f) * 0.25f;
            showQuestion_ = true;
            squash_ = 0.0f;
            break;
        }
        case PetState::COMPLETED: {
            bob_ = fabsf(sinf(phase * 3.2f)) * 12.0f;  // happy hops
            eyesOpen_ = 1.0f;
            eyeStyle_ = 1;                         // ^ ^
            mouthStyle_ = 0;
            pawLiftL_ = 1.0f;
            pawLiftR_ = 1.0f;
            tailAngle_ = 0.9f + sinf(phase * 3.0f) * 0.3f; // tail up, waving
            headTiltX_ = sinf(phase * 2.0f) * 1.5f;
            headTiltY_ = -1.0f;
            showStar_ = true;
            squash_ = 0.0f;
            break;
        }
        case PetState::ERROR: {
            eyeStyle_ = 2;                         // X X
            mouthStyle_ = 2;                       // frown
            headTiltY_ = 5.0f;                     // head down
            earDropL_ = 1.0f;
            earDropR_ = 1.0f;
            bob_ = sinf(phase * 8.0f) * 0.8f;      // shake
            tailAngle_ = -0.5f;
            squash_ = 0.15f;
            showSweat_ = true;
            break;
        }
        case PetState::SLEEP: {
            eyeStyle_ = 3;                         // closed dash
            eyesOpen_ = 0.0f;
            mouthStyle_ = 0;
            squash_ = 0.55f;                       // lying flat
            headTiltY_ = 6.0f;                     // head resting
            earDropL_ = 0.8f;
            earDropR_ = 0.8f;
            tailAngle_ = -0.8f;
            bob_ = sinf(phase * 0.5f) * 0.6f;      // slow breathing
            showZzz_ = true;
            break;
        }
        case PetState::OFFLINE: {
            eyeStyle_ = 4;                         // dull dots
            eyesOpen_ = 0.5f;
            mouthStyle_ = 2;
            headTiltX_ = 0.0f;
            headTiltY_ = 1.0f;
            bob_ = sinf(phase * 0.4f) * 0.4f;
            tailAngle_ = 0.0f;
            squash_ = 0.0f;
            break;
        }
    }
}

void PetAnimation::draw(Adafruit_ST7789& tft, int16_t x, int16_t y) {
    canvas_.fillScreen(COLOR_BG);

    const float t = static_cast<float>(millis() - stateStartedMs_) / 1000.0f;
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t bodyBaseY = 96 + jump;
    const int16_t headBaseY = 56 + static_cast<int16_t>(headTiltY_) + jump;

    // ---- tail (behind body) ----
    drawTail();

    // ---- body ----
    drawBody();

    // ---- front paws ----
    drawPaws();

    // ---- head + face ----
    drawFace();

    // ---- state symbols ----
    drawSymbols(64, 10, t);

    const uint16_t* buf = canvas_.getBuffer();
    tft.startWrite();
    tft.setAddrWindow(x, y, SIZE, SIZE);
    uint32_t total = SIZE * SIZE;
    uint32_t off = 0;
    while (off < total) {
        const uint32_t n = (total - off > 1024) ? 1024 : (total - off);
        tft.writePixels(const_cast<uint16_t*>(buf + off), n);
        off += n;
    }
    tft.endWrite();
    (void)bodyBaseY;
    (void)headBaseY;
}

void PetAnimation::drawTail() {
    const int16_t baseX = 86;
    const int16_t baseY = 96 + static_cast<int16_t>(bob_);
    const int16_t len = 26;
    const int16_t tipX = baseX + static_cast<int16_t>(cosf(tailAngle_) * len);
    const int16_t tipY = baseY - static_cast<int16_t>(sinf(tailAngle_) * len * 0.8f) - 8;

    const int segments = 5;
    uint16_t color = (state_ == PetState::OFFLINE) ? COLOR_GRAY_DK : COLOR_BODY_DK;
    for (int i = segments; i >= 0; --i) {
        const float f = static_cast<float>(i) / segments;
        const int16_t px = baseX + static_cast<int16_t>((tipX - baseX) * f);
        const int16_t py = baseY + static_cast<int16_t>((tipY - baseY) * f);
        canvas_.fillCircle(px, py, 4 + static_cast<int16_t>(f * 2.0f), color);
    }
    canvas_.fillCircle(tipX, tipY, 4, (state_ == PetState::OFFLINE) ? COLOR_GRAY : COLOR_BODY);
}

void PetAnimation::drawBody() {
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t baseY = 96 + jump;
    const int16_t h = 34 - static_cast<int16_t>(18.0f * squash_);
    const int16_t w = 48 + static_cast<int16_t>(14.0f * squash_); // flatten when sleeping
    const int16_t cx = 64;
    const int16_t cy = baseY - h / 2;

    uint16_t bodyColor = COLOR_BODY;
    uint16_t bellyColor = COLOR_BELLY;
    if (state_ == PetState::OFFLINE) {
        bodyColor = COLOR_GRAY;
        bellyColor = COLOR_GRAY_BL;
    } else if (state_ == PetState::ERROR) {
        bodyColor = rgb565(240, 186, 150);
    }

    fillRoundRectAuto(canvas_, cx - w / 2, cy - h / 2, w, h, 12, bodyColor);
    fillRoundRectAuto(canvas_, cx - 14, cy - 2, 28, h / 2 + 2, 10, bellyColor);

    // little chest stripes
    if (state_ != PetState::OFFLINE) {
        for (int i = 0; i < 3; ++i) {
            canvas_.drawLine(cx - 8 + i * 8, cy + 2, cx - 4 + i * 8, cy + 10, COLOR_BODY_DK);
        }
    }
}

void PetAnimation::drawPaws() {
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t baseY = 110 + jump;
    const int16_t pawW = 14;
    const int16_t pawH = 16;
    const int16_t liftL = static_cast<int16_t>(pawLiftL_ * 8.0f);
    const int16_t liftR = static_cast<int16_t>(pawLiftR_ * 8.0f);
    uint16_t color = COLOR_BODY;
    if (state_ == PetState::OFFLINE) color = COLOR_GRAY;
    else if (state_ == PetState::ERROR) color = rgb565(240, 186, 150);

    // left paw
    fillRoundRectAuto(canvas_, 44, baseY - liftL, pawW, pawH, 6, color);
    // right paw
    fillRoundRectAuto(canvas_, 70, baseY - liftR, pawW, pawH, 6, color);

    // toe lines
    canvas_.drawLine(47, baseY - liftL + 10, 49, baseY - liftL + 14, COLOR_BODY_DK);
    canvas_.drawLine(51, baseY - liftL + 10, 53, baseY - liftL + 14, COLOR_BODY_DK);
    canvas_.drawLine(73, baseY - liftR + 10, 75, baseY - liftR + 14, COLOR_BODY_DK);
    canvas_.drawLine(77, baseY - liftR + 10, 79, baseY - liftR + 14, COLOR_BODY_DK);
}

void PetAnimation::drawFace() {
    const int16_t jump = static_cast<int16_t>(bob_);
    const int16_t cx = 64 + static_cast<int16_t>(headTiltX_);
    const int16_t cy = 58 + static_cast<int16_t>(headTiltY_) + jump;
    const int16_t r = 30;

    uint16_t headColor = COLOR_BODY;
    uint16_t earColor = COLOR_BODY;
    uint16_t earIn = COLOR_EAR_IN;
    uint16_t lineColor = COLOR_LINE;
    if (state_ == PetState::OFFLINE) {
        headColor = COLOR_GRAY;
        earColor = COLOR_GRAY;
        earIn = COLOR_GRAY_DK;
        lineColor = COLOR_GRAY_DK;
    } else if (state_ == PetState::ERROR) {
        headColor = rgb565(240, 186, 150);
        earColor = headColor;
    }

    // ears (behind head)
    const int16_t earDropPxL = static_cast<int16_t>(earDropL_ * 7.0f);
    const int16_t earDropPxR = static_cast<int16_t>(earDropR_ * 7.0f);
    // left ear: base (cx-28, cy-14) -> tip (cx-20, cy-44+drop) -> inner (cx-4, cy-18)
    canvas_.fillTriangle(cx - 28, cy - 12 + earDropPxL / 2,
                         cx - 20, cy - 46 + earDropPxL,
                         cx - 4, cy - 16 + earDropPxL / 2, earColor);
    // right ear
    canvas_.fillTriangle(cx + 28, cy - 12 + earDropPxR / 2,
                         cx + 20, cy - 46 + earDropPxR,
                         cx + 4, cy - 16 + earDropPxR / 2, earColor);
    // inner ears
    canvas_.fillTriangle(cx - 22, cy - 14 + earDropPxL / 2,
                         cx - 19, cy - 32 + earDropPxL,
                         cx - 11, cy - 16 + earDropPxL / 2, earIn);
    canvas_.fillTriangle(cx + 22, cy - 14 + earDropPxR / 2,
                         cx + 19, cy - 32 + earDropPxR,
                         cx + 11, cy - 16 + earDropPxR / 2, earIn);

    // head
    canvas_.fillCircle(cx, cy, r, headColor);
    canvas_.fillCircle(cx - 20, cy + 4, 7, headColor);
    canvas_.fillCircle(cx + 20, cy + 4, 7, headColor);

    // head stripes
    if (state_ != PetState::OFFLINE) {
        canvas_.fillRoundRect(cx - 16, cy - 32, 7, 9, 3, COLOR_BODY_DK);
        canvas_.fillRoundRect(cx - 4, cy - 35, 7, 10, 3, COLOR_BODY_DK);
        canvas_.fillRoundRect(cx + 9, cy - 33, 7, 9, 3, COLOR_BODY_DK);
    }

    // eyes
    const int16_t eyeY = cy - 4;
    const int16_t eyeDX = static_cast<int16_t>(eyeX_ * 2.5f);
    drawEye(canvas_, cx - 15 + eyeDX, eyeY, eyeStyle_, eyesOpen_, eyeX_);
    drawEye(canvas_, cx + 15 + eyeDX, eyeY, eyeStyle_, eyesOpen_, eyeX_);

    // nose
    canvas_.fillTriangle(cx - 4, cy + 6, cx + 4, cy + 6, cx, cy + 11,
                         (state_ == PetState::OFFLINE) ? COLOR_GRAY_DK : COLOR_EAR_IN);

    // mouth
    const int16_t my = cy + 13;
    if (mouthStyle_ == 0) { // smile W
        canvas_.drawLine(cx - 9, my, cx - 4, my + 5, lineColor);
        canvas_.drawLine(cx + 9, my, cx + 4, my + 5, lineColor);
        canvas_.drawLine(cx - 3, my + 5, cx + 3, my + 5, lineColor);
    } else if (mouthStyle_ == 1) { // open talking
        canvas_.fillCircle(cx, my + 5, 6, COLOR_LINE);
        canvas_.fillCircle(cx, my + 5, 4, COLOR_RED);
    } else if (mouthStyle_ == 2) { // frown
        canvas_.drawLine(cx - 9, my + 7, cx - 3, my + 2, lineColor);
        canvas_.drawLine(cx + 9, my + 7, cx + 3, my + 2, lineColor);
    } else if (mouthStyle_ == 3) { // O
        canvas_.fillCircle(cx, my + 4, 6, COLOR_LINE);
        canvas_.fillCircle(cx, my + 4, 4, COLOR_RED);
    }

    // whiskers
    canvas_.drawLine(cx - 22, cy + 6, cx - 50, cy + 2, lineColor);
    canvas_.drawLine(cx - 22, cy + 12, cx - 52, cy + 14, lineColor);
    canvas_.drawLine(cx + 22, cy + 6, cx + 50, cy + 2, lineColor);
    canvas_.drawLine(cx + 22, cy + 12, cx + 52, cy + 14, lineColor);

    // blush
    canvas_.fillCircle(cx - 24, cy + 10, 5, COLOR_BLUSH);
    canvas_.fillCircle(cx + 24, cy + 10, 5, COLOR_BLUSH);
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
