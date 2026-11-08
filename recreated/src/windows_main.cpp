#include "asset_catalog.h"
#include "entity_behavior.h"
#include "game_state.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool gDebugEnabled = false;
// Opt-in frame trace used only while reconstructing behaviour. Pass --log on
// the command line to append diagnostics to bgw_debug.log next to the exe.
void debugLog(const char* format, ...) {
    if (!gDebugEnabled) return;
    char buffer[1024]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::ofstream output("bgw_debug.log", std::ios::app);
    if (output) {
        output << buffer << '\n';
    }
}
constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kTile = 32;
// PTR_PTR_0045a39c indexes this table with difficulty-1 in states 2 and 10.
constexpr const char* kLevelNames[5] = {"EASY", "NOMAL", "HARD", "HARD1",
                                        "HARD2"};
// Development aid: --shot=<dir> saves the raw 320x240 back buffer as a
// 24-bit BMP every SHOT_INTERVAL frames so pixel comparisons are not polluted
// by the screen-copy path used for the interactive window. --shot-every=N
// overrides the interval for frame-accurate comparisons.
int gShotInterval = 30;
std::string gShotDir;
bool gFastTimer = false;

void saveBackBuffer(HDC source, int width, int height) {
    if (gShotDir.empty()) return;
    static int counter = 0;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\shot_%04d.bmp", gShotDir.c_str(),
                  counter++);
    HDC mem = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, width, height);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, bitmap));
    BitBlt(mem, 0, 0, width, height, source, 0, 0, SRCCOPY);

    BITMAPFILEHEADER header{};
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height; // bottom-up, like the shipped assets
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = BI_RGB;
    const int stride = ((width * 3 + 3) / 4) * 4;
    info.biSizeImage = stride * height;
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + info.biSizeImage;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    BITMAPINFO dib{};
    dib.bmiHeader = info;
    GetDIBits(source, bitmap, 0, height, pixels.data(), &dib, DIB_RGB_COLORS);

    std::ofstream output(path, std::ios::binary);
    if (output) {
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(reinterpret_cast<const char*>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
    }
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
}
// EntityRecord +0x80/+0x84 hotspot offsets are round(half * scale / 100) with
// the type-1 scale fields set to 40, so a 40x40 sprite gets a 24x24 hitbox.
constexpr int kBoxHalf = 12;
int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) --quotient;
    return quotient;
}
// FUN_004562F4 seeds the layer-0 scroll accumulator to X = 0x000A00 (80px) and
// Y = 0x03E040 (world 7938px, row ~248: the bottom of the shaft), and
// FUN_00454084 spawns the player at entity (0x1000, 0x1640) = screen (128, 178).
// Entity X/Y are screen-space pixels added to the scroll, so the player's world
// spawn is (80+128, 7938+178) = (208, 8116) near the bottom of the 256-row map.
constexpr int kSpawnX = 208;
constexpr float kSpawnY = 8116.0f;
constexpr int kSpawnCameraX = 80;
constexpr int kSpawnCameraY = 7938;
// FUN_004562F4 seeds the layer-1 (background) scroll accumulator to X = 0x400
// (32px), so bg002.bmp is first blitted at x = -32 (and again at +288 because
// the layer wraps). Layer 1 scrolls at one eighth of the layer-0 speed.
constexpr int kBackgroundScrollSeed = 0x400;

struct App {
    HBITMAP title = nullptr;
    HBITMAP background = nullptr;
    HBITMAP options = nullptr;
    HBITMAP player = nullptr;
    HBITMAP power = nullptr;
    HBITMAP tiles = nullptr;
    // chr002 (wall-collapse chunks), chr004 (100/500/1000 popups) and chr005
    // (jump puff) are sprite surfaces 4, 6 and 8 in data\001surf.dat.
    HBITMAP collapse = nullptr;
    HBITMAP scorePopup = nullptr;
    HBITMAP jumpPuff = nullptr;
    HBITMAP font = nullptr;
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr;
    HWND window = nullptr;
    bgw::AssetCatalog catalog;
    bool playing = false;
    bool left = false;
    bool right = false;
    bool jump = false;
    bool grounded = false;
    bool cleared = false;
    bool playerDead = false;
    bool gameOver = false;
    bool scoreRecorded = false;
    int playerX = 128;
    float playerY = 178.0f;
    float playerVelocityY = 0.0f;
    int cameraX = 0;
    int cameraY = 0;
    int backgroundScrollX = kBackgroundScrollSeed;
    int backgroundScrollY = 0;
    bool backgroundReseed = false;
    // FUN_00453b64 animation: +0x3c timer, +0x14 sprite bank, +0x18 frame. The
    // sprite frame blitted each frame is (+0x14 + +0x18) into the type table.
    int playerSpriteIndex = 0;
    int playerAnimFrame = 0;
    int playerAnimTick = 0;
    int playerFrame = 0;
    int frameCounter = 0;
    // FUN_00454DB8 keeps a global animation counter (PTR_DAT_0045a2cc) for the
    // five 40x40 bonus tiles. It is reset when state 10 is first entered and
    // incremented after every draw; the tile source column changes every 8
    // frames through {0x28,0x50,0x78,0x50}.
    int bonusAnimation = 0;
    bool wasPostClear = false;
    int playerDirection = 1;
    // FUN_00452774 keeps one contact counter on the player (+0x40). It is
    // incremented once per frame while any sampled column is solid and is
    // reset to zero only when a tile actually transitions. It is deliberately
    // not reset when the player walks onto a different column.
    int solidContactFrames = 0;
    int playerCollisionCount = 0;
    int score = 0;
    int lives = 3;
    int breakableScoreIndex = 0;
    int breakStreakTimer = 0;
    int breakableCount = 0;
    // FUN_00453B64: the player accrues jump energy in steps of 10 up to 30;
    // each successive jump while airborne is weaker and grants 50 points.
    int jumpEnergy = 0;
    // FUN_004562F4 seeds the play timer at 99; FUN_00455228 drops one unit per
    // 0x78 frames and enters the miss state when it reaches zero.
    int timeRemaining = 99;
    int timeAccumulator = 0;
    std::array<int, 10> highScores{};
    std::array<std::string, 10> highScoreNames{};
    bgw::EntityPool entityPool;
    std::string rankName = "PLAYER";
    std::vector<std::uint16_t> initialTiles;
    bgw::GameObjectState runtimeObject;
    bgw::GameStateMachine* stateMachine = nullptr;
    bgw::EntityRecord playerEntity{};
    // FormDestroy (VA 0x456F7C) saves [button] shot/dash and [env] debugflg
    // alongside the option flags. The reconstruction does not model the
    // button remap or the debug counter, so preserve the loaded values.
    int buttonShot = 0;
    int buttonDash = 1;
    bool debugFlag = false;
};

void drawText(App& app, int x, int y, const std::string& text, int style);
void drawFrame(App& app);
void writeIniSettings(App& app);
void writeRankFile(App& app);
int insertRankScore(App& app, std::int32_t score);

class Win32Runtime final : public bgw::RuntimeApi {
public:
    explicit Win32Runtime(App& app) : app_(app) {}
    ~Win32Runtime() override { stopMusic(); }

    void stopMusic() override {
        mciSendStringA("stop bgw_music", nullptr, 0, nullptr);
        mciSendStringA("close bgw_music", nullptr, 0, nullptr);
        musicOpen_ = false;
    }

    void playMidi(const char* name, bool loop) override {
        stopMusic();
        std::string path = std::string(name) + ".mid";
        std::string command = "open \"" + path + "\" type sequencer alias bgw_music";
        if (mciSendStringA(command.c_str(), nullptr, 0, nullptr) != 0) return;
        const char* mode = loop ? " repeat" : "";
        std::string play = std::string("play bgw_music") + mode;
        musicOpen_ = mciSendStringA(play.c_str(), nullptr, 0, nullptr) == 0;
    }

    bool musicFinished() override {
        if (!musicOpen_) return true;
        char mode[32]{};
        if (mciSendStringA("status bgw_music mode", mode, sizeof(mode), nullptr) != 0) {
            musicOpen_ = false;
            return true;
        }
        return std::string(mode) != "playing";
    }
    void clearFrame() override { drawFrame(app_); }
    void present() override { InvalidateRect(app_.window, nullptr, FALSE); }
    void updateSprites() override {}
    void updateGameObjects() override {}
    void drawText(int x, int y, const char* text, int style) override {
        // FUN_004524AC takes font-cell coordinates; the renderer works in
        // pixels.
        ::drawText(app_, x * 8, y * 8, text, style);
    }
    int insertRankScore(int32_t score) override {
        return ::insertRankScore(app_, score);
    }
    void writeRankNameByte(int32_t rank, int32_t offset, char value) override {
        if (rank < 0 || rank >= static_cast<int32_t>(app_.highScoreNames.size())) {
            return;
        }
        if (offset < 0 || offset > 3) return;
        std::string& name = app_.highScoreNames[rank];
        name.resize(4, '\0');
        name[offset] = value;
    }
    std::uint32_t timeGetTime() override {
        return static_cast<std::uint32_t>(::timeGetTime());
    }
    bool keyDown(int keyCode) override {
        return (GetAsyncKeyState(keyCode) & 0x8000) != 0;
    }

private:
    App& app_;
    bool musicOpen_ = false;
};

App* appFrom(HWND window) {
    return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

HBITMAP loadBitmap(const char* path) {
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageA(
        nullptr, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
    if (!bitmap) {
        throw std::runtime_error(std::string("cannot load bitmap: ") + path);
    }
    return bitmap;
}

void drawBitmap(HDC destination, HBITMAP bitmap, int x, int y, int width, int height,
                int sourceX = 0, int sourceY = 0, int sourceWidth = -1,
                int sourceHeight = -1, bool transparent = false) {
    if (sourceWidth < 0 || sourceHeight < 0) {
        BITMAP info{};
        GetObject(bitmap, sizeof(info), &info);
        if (sourceWidth < 0) sourceWidth = info.bmWidth;
        if (sourceHeight < 0) sourceHeight = info.bmHeight;
    }
    // The map tiles and font glyphs are blitted many times per frame, so keep a
    // single cached source DC instead of creating and destroying one per call.
    static HDC source = nullptr;
    if (!source) source = CreateCompatibleDC(destination);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(source, bitmap));
    if (transparent) {
        TransparentBlt(destination, x, y, width, height, source, sourceX, sourceY,
                       sourceWidth, sourceHeight, RGB(0, 0, 0));
    } else {
        StretchBlt(destination, x, y, width, height, source, sourceX, sourceY,
                   sourceWidth, sourceHeight, SRCCOPY);
    }
    SelectObject(source, oldBitmap);
}

void drawText(App& app, int x, int y, const std::string& text, int style = 0) {
    // Blit manually to ensure correct transparency with palette fonts
    HDC fontDc = CreateCompatibleDC(app.backDc);
    HBITMAP oldFont = static_cast<HBITMAP>(SelectObject(fontDc, app.font));
    for (unsigned char character : text) {
        const int code = static_cast<int>(character) - 0x20;
        if (code >= 0 && code < 64) {
            int sx = (code % 32) * 8;
            int sy = (code / 32) * 8 + style * 16;
            TransparentBlt(app.backDc, x, y, 8, 8, fontDc, sx, sy, 8, 8, RGB(0, 0, 0));
        }
        x += 8;
    }
    SelectObject(fontDc, oldFont);
    DeleteDC(fontDc);
}

void playSound(const char* path) {
    debugLog("sound %s", path);
    PlaySoundA(path, nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
}

// The sprite bank is a flat global frame list. data\001spr.dat lists each
// surface's cell grid in order, so frame numbers accumulate across surfaces:
//   surface 2 chr001 40x40 4x5 -> frames  0..19 (player)
//   surface 4 chr002 32x32 4x3 -> frames 20..31 (collapse chunks)
//   surface 6 chr004 32x16 1x3 -> frames 32..34 (100/500/1000 popups)
//   surface 8 chr005 32x32 2x1 -> frames 35..36 (jump puff)
struct SpriteFrameInfo {
    HBITMAP bitmap = nullptr;
    int sourceX = 0;
    int sourceY = 0;
    int width = 0;
    int height = 0;
};

HBITMAP surfaceBitmap(App& app, int surface) {
    switch (surface) {
    case 2: return app.player;
    case 4: return app.collapse;
    case 6: return app.scorePopup;
    case 8: return app.jumpPuff;
    default: return nullptr;
    }
}

bool lookupSpriteFrame(App& app, int frame, SpriteFrameInfo& info) {
    int first = 0;
    for (const bgw::SpriteDefinition& sprite : app.catalog.sprites) {
        if (sprite.columns <= 0 || sprite.rows <= 0) continue;
        const int count = sprite.columns * sprite.rows;
        if (frame >= first && frame < first + count) {
            const int cell = frame - first;
            info.bitmap = surfaceBitmap(app, sprite.surface);
            info.sourceX = (cell % sprite.columns) * sprite.width;
            info.sourceY = (cell / sprite.columns) * sprite.height;
            info.width = sprite.width;
            info.height = sprite.height;
            return info.bitmap != nullptr;
        }
        first += count;
    }
    return false;
}

void spawnEntityEffect(App& app, int x, int y, int typeIndex, int animFrame) {
    // FUN_00453224 and FUN_00452774 allocate group-2 effect slots through
    // FUN_00454084. The type table at PTR_DAT_0045A498 (VA 0x459F4C, indexed
    // by type*0x34) holds base frame / +0x74 / +0x78 / cull flag:
    //   type 2 -> {20,0,4,0,0,0,1,0,0}  (chr002, 12 collapse frames)
    //   type 3 -> {32,0,4,0,0,0,1,0,0}  (chr004, 3 score popups)
    //   type 4 -> {35,0,4,0,0,0,1,0,0}  (chr005, 2 jump-puff frames)
    // The caller writes the frame offset into +0x18 (animFrame).
    static const bgw::EntityTypeFields kTypeDefs[3] = {
        {20, 0, 4, 0, 0, 0, 1, 0, 0},
        {32, 0, 4, 0, 0, 0, 1, 0, 0},
        {35, 0, 4, 0, 0, 0, 1, 0, 0},
    };
    const int definition = std::clamp(typeIndex - 2, 0, 2);
    const int slot = app.entityPool.spawn(2, typeIndex, x << 5, y << 5, 0, 0, 0,
                                          kTypeDefs[definition]);
    if (slot >= 0) {
        app.entityPool.slot(slot).animFrame = animFrame;
    }
}

// FUN_0045720C keeps the ten-entry ranking table in data\rank.dat as 12-byte
// records: an int32 score followed by an 8-byte name.
void writeRankFile(App& app) {
    std::ofstream output("data/rank.dat", std::ios::binary | std::ios::trunc);
    if (!output) return;
    for (std::size_t index = 0; index < app.highScores.size(); ++index) {
        const std::int32_t score = app.highScores[index];
        output.write(reinterpret_cast<const char*>(&score), sizeof(score));
        char name[8]{};
        const std::string& source = app.highScoreNames[index];
        std::copy_n(source.begin(), std::min<std::size_t>(source.size(), 8), name);
        output.write(name, sizeof(name));
    }
}

int insertRankScore(App& app, std::int32_t score) {
    for (std::size_t index = 0; index < app.highScores.size(); ++index) {
        if (app.highScores[index] < score) {
            for (std::size_t move = app.highScores.size() - 1; move > index; --move) {
                app.highScores[move] = app.highScores[move - 1];
                app.highScoreNames[move] = app.highScoreNames[move - 1];
            }
            app.highScores[index] = score;
            // The new name starts as four zero bytes; FUN_0045729C fills them
            // in one character at a time. FUN_0045720C only mutates memory;
            // data\rank.dat is rewritten by the shutdown config-save path.
            app.highScoreNames[index] = std::string(4, '\0');
            return static_cast<int>(index);
        }
    }
    return -1;
}

void loadScores(App& app) {
    std::ifstream input("data/rank.dat", std::ios::binary);
    if (!input) return;
    for (std::size_t index = 0; index < app.highScores.size(); ++index) {
        std::int32_t score = 0;
        if (!input.read(reinterpret_cast<char*>(&score), sizeof(score))) break;
        app.highScores[index] = score;
        char name[8]{};
        input.read(name, sizeof(name));
        std::size_t length = 0;
        while (length < sizeof(name) && name[length] != '\0') ++length;
        app.highScoreNames[index].assign(name, length);
    }
}

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

int readIniInt(const char* path, const char* section, const char* key,
               int fallback) {
    std::ifstream input(path);
    if (!input) return fallback;
    std::string currentSection;
    for (std::string line; std::getline(input, line);) {
        line = trimCopy(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        if (currentSection != section) continue;
        if (trimCopy(line.substr(0, equals)) != key) continue;
        try {
            return std::stoi(trimCopy(line.substr(equals + 1)));
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool readIniBool(const char* path, const char* section, const char* key,
                 bool fallback) {
    std::ifstream input(path);
    if (!input) return fallback;
    std::string currentSection;
    for (std::string line; std::getline(input, line);) {
        line = trimCopy(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        if (currentSection != section) continue;
        if (trimCopy(line.substr(0, equals)) != key) continue;
        std::string value = trimCopy(line.substr(equals + 1));
        for (char& character : value) character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
        return value == "ON" || value == "1" || value == "TRUE";
    }
    return fallback;
}

// FormDestroy (VA 0x456F7C) writes its settings through the TIniFile object:
// [button] shot/dash, [env] softvsync/waitvsync/debugflg/gamelv. It updates
// existing keys in place, appends a missing key after the last key line of its
// section, and leaves every other line and section untouched. Only a graceful
// window close reaches it, so an aborted run does not persist the option page.
void writeIniSettings(App& app) {
    std::ifstream input("game.ini");
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }

    struct Setting {
        const char* section;
        const char* key;
        std::string value;
    };
    const auto onOff = [](bool enabled) {
        return std::string(enabled ? "ON" : "OFF");
    };
    std::vector<Setting> settings{
        {"button", "shot", std::to_string(app.buttonShot)},
        {"button", "dash", std::to_string(app.buttonDash)},
        {"env", "softvsync",
         onOff(app.stateMachine && app.stateMachine->vsyncEnabled())},
        {"env", "waitvsync",
         onOff(app.stateMachine && app.stateMachine->waitEnabled())},
        {"env", "debugflg", onOff(app.debugFlag)},
        {"env", "gamelv",
         std::to_string(app.stateMachine ? app.stateMachine->difficulty() : 1)},
    };
    std::vector<bool> applied(settings.size(), false);

    std::vector<std::string> output;
    std::string currentSection;
    for (const std::string& line : lines) {
        const std::string trimmed = trimCopy(line);
        if (trimmed.size() >= 2 && trimmed.front() == '[' &&
            trimmed.back() == ']') {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            output.push_back(line);
            continue;
        }
        const std::size_t equals = trimmed.find('=');
        bool replaced = false;
        if (equals != std::string::npos) {
            const std::string key = trimCopy(trimmed.substr(0, equals));
            for (std::size_t index = 0; index < settings.size(); ++index) {
                if (applied[index] || currentSection != settings[index].section ||
                    key != settings[index].key) {
                    continue;
                }
                output.push_back(std::string(settings[index].key) + "=" +
                                 settings[index].value);
                applied[index] = true;
                replaced = true;
                break;
            }
        }
        if (!replaced) output.push_back(line);
    }

    for (std::size_t index = 0; index < settings.size(); ++index) {
        if (applied[index]) continue;
        std::string section;
        std::size_t insertAt = output.size();
        bool sectionSeen = false;
        for (std::size_t line = 0; line < output.size(); ++line) {
            const std::string trimmed = trimCopy(output[line]);
            if (trimmed.size() >= 2 && trimmed.front() == '[' &&
                trimmed.back() == ']') {
                section = trimmed.substr(1, trimmed.size() - 2);
                if (section == settings[index].section) {
                    sectionSeen = true;
                    insertAt = line + 1;
                }
            } else if (sectionSeen && section == settings[index].section &&
                       trimmed.find('=') != std::string::npos) {
                insertAt = line + 1;
            }
        }
        if (!sectionSeen) {
            output.push_back(std::string("[") + settings[index].section + "]");
            insertAt = output.size();
        }
        output.insert(output.begin() + insertAt,
                      std::string(settings[index].key) + "=" +
                          settings[index].value);
        applied[index] = true;
    }

    std::ofstream result("game.ini", std::ios::trunc);
    if (!result) return;
    for (const std::string& line : output) {
        result << line << '\n';
    }
}

class Win32EntityCallbacks final : public bgw::EntityCallbacks {
public:
    explicit Win32EntityCallbacks(App& app) : app_(app) {}

    bool collides(bgw::EntityRecord& entity, std::int32_t, std::int32_t) override {
        // FUN_004535C8 / FUN_004525A4: compare the object box (screen space,
        // since world scroll has already been applied) against the player box.
        const bgw::EntityTypeFields& fields =
            app_.entityPool.typeFields(entity.slotIndex);
        const std::int32_t cx = entity.xFixed >> 5;
        const std::int32_t cy = entity.yFixed >> 5;
        const std::int32_t left = cx - fields.halfWidth + fields.offsetX;
        const std::int32_t right = cx + fields.halfWidth - fields.offsetX;
        const std::int32_t top = cy - fields.halfHeight + fields.offsetY;
        const std::int32_t bottom = cy + fields.halfHeight - fields.offsetY;
        const std::int32_t px = app_.playerX - app_.cameraX;
        const std::int32_t py = static_cast<std::int32_t>(app_.playerY);
        return bgw::EntityPool::aabbOverlap(left, top, right, bottom,
                                             px, py, px + 40, py + 40);
    }

    void onPlayerCollision(bgw::EntityRecord&) override {}

    void spawnEffect(bgw::EntityRecord&) override {
        playSound("wav/break.wav");
    }

    void enterMissState() override {
        if (app_.stateMachine) app_.stateMachine->enterMiss();
    }

    void enterClearState() override {
        if (app_.stateMachine) app_.stateMachine->enterClear();
    }

private:
    App& app_;
};

bool solidTile(std::uint16_t value) {
    return value == 1 || (value >= 4 && value <= 15);
}

// FUN_00452774 thresholds: family 4..7 advances after 8 contact frames,
// 8..11 after 4, and 12..15 after 2. A transition resets the shared counter.
int animatedThreshold(std::uint16_t value) {
    if (value >= 4 && value <= 7) return 8;
    if (value >= 8 && value <= 11) return 4;
    if (value >= 12 && value <= 15) return 2;
    return 0;
}

bool advanceAnimatedTile(App& app, int column, int row) {
    std::uint16_t& value = app.catalog.map.tiles[
        static_cast<std::size_t>(row) * app.catalog.map.columns + column];
    const int threshold = animatedThreshold(value);
    if (threshold == 0) return false;
    if (app.solidContactFrames <= threshold) return false;
    // FUN_00452774 chains 4->5->6->7->empty, 8->9->10->11->empty and
    // 12->13->14->15->empty. The chunk frame is the pre-transition value - 4,
    // giving frames 20..31 across all three families.
    const std::uint16_t next =
        (value == 7 || value == 11 || value == 15) ? 0 : value + 1;
    spawnEntityEffect(app, column * kTile + 16,
                      static_cast<int>(app.playerY) + 32, 2, value - 4);
    playSound("wav/break.wav");
    value = next;
    app.solidContactFrames = 0;
    return true;
}

std::uint16_t tileAt(const App& app, int column, int row) {
    if (column < 0 || row < 0 || column >= app.catalog.map.columns ||
        row >= app.catalog.map.rows) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(row) * app.catalog.map.columns + column;
    return index < app.catalog.map.tiles.size() ? app.catalog.map.tiles[index] : 0;
}

void drawMap(App& app) {
    // FUN_00454535 draws the map layers back-to-front. For level 1 the fixed
    // full-screen background layer (map\001.map -> bg002.bmp, a blue gradient)
    // is drawn first, then the tiled terrain layer on top. Map tiles blit with
    // transparency (color key = black), so tile 0 (empty shaft) reveals the
    // background underneath.
    if (app.background) {
        // FUN_00450a5c wraps the single 320x240 layer tile, so the layer is
        // blitted at its scroll offset and once more one screen to the right.
        RECT full{0, 0, kWidth, kHeight};
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(app.backDc, &full, black);
        DeleteObject(black);
        int offsetX = -(app.backgroundScrollX >> 5) % kWidth;
        if (offsetX > 0) offsetX -= kWidth;
        for (int x = offsetX; x < kWidth; x += kWidth) {
            drawBitmap(app.backDc, app.background, x, 0, kWidth, kHeight);
        }
    } else {
        RECT full{0, 0, kWidth, kHeight};
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(app.backDc, &full, black);
        DeleteObject(black);
    }

    const int firstRow = std::max(0, app.cameraY / kTile - 1);
    const int lastRow = std::min(app.catalog.map.rows, (app.cameraY + kHeight) / kTile + 2);
    const int firstColumn = std::max(0, app.cameraX / kTile - 1);
    const int lastColumn = std::min(app.catalog.map.columns, (app.cameraX + kWidth) / kTile + 2);
    for (int row = firstRow; row < lastRow; ++row) {
        for (int column = firstColumn; column < lastColumn; ++column) {
            const std::uint16_t value = tileAt(app, column, row);
            if (value == 0) continue;
            // FUN_00450de4 fills a 4x4 atlas from the map bitmap (bg001); tile
            // value v maps to cell (v % 4, v / 4).
            const int x = column * kTile - app.cameraX;
            const int y = row * kTile - app.cameraY;
            drawBitmap(app.backDc, app.tiles, x, y, kTile, kTile,
                       (value % 4) * kTile, (value / 4) * kTile, kTile, kTile, true);
        }
    }
}

void drawTextCentered(App& app, int y, const std::string& text, int style = 0) {
    // FUN_004524ac: a centered string starts at ((40 - length) >> 1) cells.
    const int cells = (40 - static_cast<int>(text.size())) / 2;
    drawText(app, cells * 8, y, text, style);
}

// FUN_004574c0: the ten-entry ranking table from data\rank.dat.
void drawRanking(App& app) {
    drawTextCentered(app, 2 * 8, "- RANKING -", 2);
    drawText(app, 9 * 8, 5 * 8, "RANK", 2);
    drawText(app, 0x10 * 8, 5 * 8, "NAME", 2);
    drawText(app, 0x1a * 8, 5 * 8, "SCORE", 2);

    for (int i = 0; i < 10; ++i) {
        const int y = (i * 2 + 7) * 8;
        const char* suffix = i == 0 ? "ST" : (i == 1 ? "ND" : (i == 2 ? "RD" : "TH"));
        char rank[8]{};
        std::snprintf(rank, sizeof(rank), "%2d%s", i + 1, suffix);
        drawText(app, 9 * 8, y, rank, 0);

        // The record name is copied as exactly four bytes; NULs are skipped
        // by the glyph loop but still advance one cell each.
        std::string name = app.highScoreNames[i];
        name.resize(4, '\0');
        if (name.size() > 4) name.resize(4);
        drawText(app, 0x10 * 8, y, name, 0);

        char score[16]{};
        std::snprintf(score, sizeof(score), "%9d", app.highScores[i]);
        drawText(app, 0x16 * 8, y, score, 0);
    }
}

// FUN_0045552C: title screen (GAME START / OPTION / EXIT), the option page,
// and the ranking page.
void drawMenu(App& app) {
    auto* sm = app.stateMachine;
    if (!sm) return;
    const int page = sm->menuPage();
    if (page == 0) {
        drawBitmap(app.backDc, app.title, 0, 0, kWidth, kHeight);
        drawTextCentered(app, 0x13 * 8, "CREATED BY E.HASHIMOTO", 0);
        const char* items[3] = {"GAME START", "OPTION", "EXIT"};
        for (int i = 0; i < 3; ++i) {
            drawTextCentered(app, (0x16 + i * 2) * 8, items[i], 2);
        }
        const int sel = sm->menuIndex();
        const int y = (0x16 + sel * 2) * 8;
        drawText(app, 0x0e * 8, y, "<", 1);
        drawText(app, 0x19 * 8, y, ">", 1);
        return;
    }

    drawBitmap(app.backDc, app.options, 0, 0, kWidth, kHeight);
    if (page == 2) {
        drawRanking(app);
        drawTextCentered(app, 0x1c * 8, "BUTTON TO BACK", 1);
        return;
    }

    drawTextCentered(app, 4 * 8, "- OPTION -", 2);
    drawText(app, 5 * 8, 10 * 8, "GAME LEVEL", 2);
    drawText(app, 5 * 8, 0x0c * 8, "JUMP BUTTON ASSIGN", 2);
    drawText(app, 5 * 8, 0x0e * 8, "WAIT", 2);
    drawText(app, 5 * 8, 0x10 * 8, "SOFTWARE VSYNC", 2);
    drawText(app, 0x19 * 8, 0x12 * 8, "RANKING", 2);
    drawText(app, 0x19 * 8, 0x14 * 8, "EXIT", 2);

    // The level string table is indexed with `difficulty - 1` and stores
    // EASY/NOMAL/HARD for levels 1..3.
    static const char* difficultyNames[3] = {"EASY", "NOMAL", "HARD"};
    int difficulty = std::clamp(sm->difficulty(), 1, 3);
    drawText(app, 0x19 * 8, 10 * 8, difficultyNames[difficulty - 1], 0);

    const std::string button = "BUTTON " + std::to_string(sm->jumpButton());
    drawText(app, 0x19 * 8, 0x0c * 8, button, 0);
    drawText(app, 0x19 * 8, 0x0e * 8, sm->waitEnabled() ? "ON" : "OFF", 0);
    drawText(app, 0x19 * 8, 0x10 * 8, sm->vsyncEnabled() ? "ON" : "OFF", 0);

    const int sel = sm->optionIndex();
    drawText(app, 0x18 * 8, (sel * 2 + 10) * 8, "<", 1);
    drawText(app, 0x22 * 8, (sel * 2 + 10) * 8, ">", 1);
}

void drawHud(App& app) {
    // FUN_00455228. All coordinates are in 8px font cells. The original first
    // clears the 64px-wide right column via FUN_004523dc, which is a
    // DDBLT_COLORFILL (flags 0x1000400 = DDBLT_WAIT | DDBLT_COLORFILL) of
    // rect (256,0)-(320,240) with DDBLTFX.dwFillColor = 0, i.e. solid black.
    RECT panel{256, 0, 320, kHeight};
    HBRUSH panelBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(app.backDc, &panel, panelBrush);
    DeleteObject(panelBrush);

    char buffer[16]{};
    drawText(app, 0x21 * 8, 2 * 8, "HIGH", 1);
    drawText(app, 0x22 * 8, 3 * 8, "SCORE", 1);
    // FUN_00455228 line 597 promotes the live score into the high-score slot
    // as soon as it exceeds the stored best.
    int highScore = *std::max_element(app.highScores.begin(), app.highScores.end());
    if (app.score > highScore) highScore = app.score;
    wsprintfA(buffer, "%6d", highScore);
    drawText(app, 0x21 * 8, 4 * 8, buffer, 0);

    drawText(app, 0x22 * 8, 8 * 8, "SCORE", 1);
    wsprintfA(buffer, "%6d", app.score);
    drawText(app, 0x21 * 8, 9 * 8, buffer, 0);

    drawText(app, 0x22 * 8, 0xd * 8, "POWER", 1);
    // FUN_00455228 blits the 24x40 gauge frame from chr003.bmp at (288,112)
    // and then 30 eight-pixel bars bottom-up at (301,145-i). A bar uses source
    // row 1 (solid green) while i <= 30-jumpEnergy, otherwise row 0 (grey).
    if (app.power) {
        const int filled = std::clamp(30 - app.jumpEnergy, 0, 30);
        for (int i = 1; i <= 30; ++i) {
            const int sourceY = i <= filled ? 1 : 0;
            drawBitmap(app.backDc, app.power, 301, 145 - i, 8, 1, 0, sourceY, 8, 1,
                       false);
        }
        drawBitmap(app.backDc, app.power, 288, 112, 24, 40, 8, 0, 24, 40, true);
    }

    drawText(app, 0x23 * 8, 0x1a * 8, "TIME", 1);
    wsprintfA(buffer, "%3d", app.timeRemaining);
    drawText(app, 0x24 * 8, 0x1b * 8, buffer, 0);

    // FUN_00455228_00455461: only states 2/3 decrement the timer. The shipped
    // 0x78-frame unit is ~1 s only because its free-running loop advances
    // ~122 fps; with the loop locked to 60 fps the equivalent is 60 frames
    // (threshold 0x3B makes the pre-increment comparison tick on the 60th),
    // keeping one second per unit. Reaching zero starts the miss sequence. The
    // timer is advanced after TIME has been drawn, matching the original order.
    if (app.stateMachine) {
        const bgw::GameState state = app.stateMachine->state();
        if (state == bgw::GameState::Gameplay ||
            state == bgw::GameState::LevelIntro) {
            const int threshold = gFastTimer ? 0x01 : 0x3B;
            if (++app.timeAccumulator > threshold) {
                app.timeAccumulator = 0;
                if (app.timeRemaining > 0) --app.timeRemaining;
            }
            if (app.timeRemaining == 0) {
                app.stateMachine->setScore(app.score);
                app.stateMachine->enterMiss();
            }
        }
    }
}

// FUN_00454535 states 6/7: the full-screen bg002 backdrop plus the ranking
// table. FUN_0045729C additionally draws the current alphabet character at the
// name-entry cursor position while the name is incomplete.
void drawRankingScreen(App& app) {
    drawBitmap(app.backDc, app.background, 0, 0, kWidth, kHeight);
    drawRanking(app);
    auto* sm = app.stateMachine;
    if (sm && sm->state() == bgw::GameState::Rank && sm->nameLength() < 3) {
        const char text[2] = {sm->currentNameChar(), '\0'};
        drawText(app, (0x10 + sm->nameLength()) * 8,
                 (sm->rankIndex() * 2 + 7) * 8, text, 1);
    }
}

// FUN_00454DB8 (state 10): bg002 backdrop, five 40x40 tiles cut from the
// chr001 atlas, the normal HUD, then the level/tally text. The live score is
// a290, which the HUD prints under SCORE and promotes into HIGH SCORE; the
// TOTAL line prints a0c8, the remaining tally.
void drawPostClear(App& app) {
    drawBitmap(app.backDc, app.background, 0, 0, kWidth, kHeight);
    static const int kPhaseX[4] = {0x28, 0x50, 0x78, 0x50};
    const int phaseX = kPhaseX[(app.bonusAnimation / 8) & 3];
    for (int index = 0; index < 5; ++index) {
        drawBitmap(app.backDc, app.player, index * 0x28 + 0x1c, 0xb8, 0x28,
                   0x28, phaseX, 0xa0, 0x28, 0x28, true);
    }
    auto* sm = app.stateMachine;
    if (sm) app.score = sm->score();
    drawHud(app);

    const int level = std::clamp(sm ? sm->difficulty() : 1, 1, 5);
    drawText(app, 0x0b * 8, 6 * 8,
             std::string("LEVEL ") + kLevelNames[level - 1], 2);
    drawText(app, 0x0b * 8, 8 * 8, "- CLEAR! -", 2);
    drawText(app, 4 * 8, 0x0d * 8, "BUNGLUWA BONUS       PTS", 2);
    drawText(app, 4 * 8, 0x0f * 8, "TIME     BONUS       PTS", 2);
    drawText(app, 4 * 8, 0x13 * 8, "TOTAL    BONUS       PTS", 2);

    char buffer[16]{};
    wsprintfA(buffer, "%6d", app.breakableCount * 100 * level);
    drawText(app, 0x12 * 8, 0x0d * 8, buffer, 0);
    wsprintfA(buffer, "%6d", app.timeRemaining * 100);
    drawText(app, 0x12 * 8, 0x0f * 8, buffer, 0);
    wsprintfA(buffer, "%6d", sm ? sm->remainingBonus() : 0);
    drawText(app, 0x12 * 8, 0x13 * 8, buffer, 0);
}

void drawFrame(App& app) {
    auto* sm = app.stateMachine;
    const bgw::GameState state = sm ? sm->state() : bgw::GameState::Menu;

    if (state == bgw::GameState::MenuInit || state == bgw::GameState::Menu) {
        drawMenu(app);
        return;
    }
    if (state == bgw::GameState::Rank || state == bgw::GameState::Result) {
        drawRankingScreen(app);
        return;
    }
    if (state == bgw::GameState::PostClear) {
        // The original draws the first bonus frame immediately on entering
        // state 10, before its phase counters advance.
        if (!app.wasPostClear) {
            app.bonusAnimation = 0;
            app.wasPostClear = true;
        }
        drawPostClear(app);
        ++app.bonusAnimation;
        return;
    }
    app.wasPostClear = false;
    if (state == bgw::GameState::ExitDialog) {
        RECT full{0, 0, kWidth, kHeight};
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(app.backDc, &full, black);
        DeleteObject(black);
        drawTextCentered(app, 0x0e * 8, "EXIT ESC KEY", 2);
        return;
    }
    if (state == bgw::GameState::Continue) {
        // State 8 only paints the CONTINUE prompt over the last field frame
        // (FUN_00454535 does not clear the back buffer here). No shipped code
        // path sets state 8, so this screen is unreachable in normal play.
        int seconds = 9 - (sm ? sm->frameCounter() / 0x78 : 0);
        if (seconds < 0) seconds = 0;
        char countText[4]{};
        std::snprintf(countText, sizeof(countText), "%d", seconds);
        drawText(app, 0x0e * 8, 0x0d * 8, "CONTINUE", 2);
        drawText(app, 0x17 * 8, 0x0d * 8, countText, 0);
        return;
    }

    // Field states: FUN_00453E50 draws the map and entities, FUN_00455228 the
    // HUD, and the per-state block adds its overlay afterwards.
    drawMap(app);
    if (!app.playerDead && state != bgw::GameState::PostClear) {
        // FUN_004501f4 / FUN_00450240 return the sprite half-size, and the
        // blit is issued at (screen - width/2, screen - height/2). The player
        // sprite is 40x40, so the drawn cell is centered on the entity.
        drawBitmap(app.backDc, app.player, app.playerX - app.cameraX - 20,
                   static_cast<int>(app.playerY) - app.cameraY - 20, 40, 40,
                   (app.playerFrame % 4) * 40, (app.playerFrame / 4) * 40, 40, 40,
                   true);
    }
    for (const bgw::EntityRecord& entity : app.entityPool.slots()) {
        if (entity.active == 0 || entity.type == 1) continue;
        // FUN_00453E50 hides the render link on odd cooldown frames, producing
        // the recovered invincibility/expiry blink.
        if ((entity.cooldown & 1) != 0) continue;
        // FUN_00453E50 writes the surface frame as +0x14 + +0x18 and places
        // the cell at (xFixed>>5 - halfW, yFixed>>5 - halfH). Effect entities
        // are stored in world space here, so the camera is subtracted too.
        SpriteFrameInfo info;
        if (!lookupSpriteFrame(app, entity.spriteIndex + entity.animFrame, info)) continue;
        const int effectX = (entity.xFixed >> 5) - app.cameraX - info.width / 2;
        const int effectY = (entity.yFixed >> 5) - app.cameraY - info.height / 2;
        // Collapse chunks are cull-exempt in the shipped type table (+0x28 = 1)
        // and never deactivate, so the pool can hold many of them after a long
        // climb. Skipping fully off-screen blits is visually identical (GDI
        // would clip them anyway) and keeps the per-frame cost bounded.
        if (effectX + info.width <= 0 || effectX >= kWidth ||
            effectY + info.height <= 0 || effectY >= kHeight) {
            continue;
        }
        drawBitmap(app.backDc, info.bitmap, effectX, effectY, info.width, info.height,
                   info.sourceX, info.sourceY, info.width, info.height, true);
    }
    drawHud(app);

    // FUN_00454535 draws the state-2 level banner after the map and the HUD:
    // "LEVEL " plus the string table entry for the current game level at cell
    // (0x0B, 0x0E), then "- START! -" at (0x0B, 0x10), both style 2.
    if (state == bgw::GameState::Gameplay) {
        const int level = std::clamp(sm ? sm->difficulty() : 1, 1, 5);
        const std::string banner =
            std::string("LEVEL ") + kLevelNames[level - 1];
        drawText(app, 0x0b * 8, 0x0e * 8, banner, 2);
        drawText(app, 0x0b * 8, 0x10 * 8, "- START! -", 2);
    }
    if (state == bgw::GameState::GameOver) {
        // FUN_004524AC(0xB, 0xE, "GAME  OVER", centered=0, surface, style=1)
        drawText(app, 0x0b * 8, 0x0e * 8, "GAME  OVER", 1);
    }
}

void update(App& app) {
    if (!app.playing || !app.stateMachine) return;
    const bgw::GameState state = app.stateMachine->state();
    const bool runPlayer =
        state == bgw::GameState::Gameplay || state == bgw::GameState::LevelIntro ||
        ((state == bgw::GameState::Miss || state == bgw::GameState::GameOver) &&
         !app.playerDead);
    if (!runPlayer) {
        // FUN_00453E50 keeps updating the effect pool during states 4/5/9
        // even after the player render link has been switched off.
        bgw::EntityWorldState entityWorld;
        Win32EntityCallbacks entityCallbacks(app);
        app.entityPool.update(entityWorld, entityCallbacks);
        return;
    }
    const int direction = (app.right ? 1 : 0) - (app.left ? 1 : 0);
    if (direction != 0) app.playerDirection = direction;
    const int previousCameraX = app.cameraX;
    const int previousCameraY = app.cameraY;
    const bool wasGrounded = app.grounded;
    // FUN_00453b64 chooses the sprite bank/frame from the *current* vertical
    // velocity before the frame's physics runs: idle toggles frames 0/1 every
    // nine frames, airborne advances a sixteen-frame cycle every three frames.
    if (app.playerVelocityY == 0.0f) {
        if (++app.playerAnimTick > 8) {
            app.playerAnimTick = 0;
            app.playerSpriteIndex = 0;
            app.playerAnimFrame = (app.playerAnimFrame + 1) & 1;
        }
    } else {
        if (++app.playerAnimTick > 2) {
            app.playerAnimTick = 0;
            app.playerSpriteIndex = 1;
            app.playerAnimFrame += app.playerDirection;
            if (app.playerAnimFrame < 0) app.playerAnimFrame = 15;
            if (app.playerAnimFrame > 15) app.playerAnimFrame = 0;
        }
    }
    app.playerFrame = app.playerSpriteIndex + app.playerAnimFrame;
    app.playerEntity.active = 1;
    app.playerEntity.type = 1;
    app.playerEntity.xFixed = app.playerX << 5;
    app.playerEntity.yFixed = static_cast<int32_t>(app.playerY) << 5;
    bgw::EntityWorldState entityWorld;
    entityWorld.playerInput = direction;
    // Entities are stored in world space; the camera offset is applied at draw
    // time, so the recovered per-frame scroll is not double-applied here.
    Win32EntityCallbacks entityCallbacks(app);
    bgw::updateEntity(app.playerEntity, entityWorld, entityCallbacks);
    app.entityPool.update(entityWorld, entityCallbacks);
    // FUN_00452E8C / FUN_00452CC0: horizontal collision uses the entity's
    // 24x24 collision box. The 40x40 sprite is inset by the recovered
    // +0x80/+0x84 hotspot offsets, round(half * 40 / 100) = 8, so the box is
    // [centre-12, centre+11] on each axis.
    if (direction != 0) {
        const int step = direction * 2;
        const int topRow = floorDiv(static_cast<int>(app.playerY) - kBoxHalf, kTile);
        const int bottomRow =
            floorDiv(static_cast<int>(app.playerY) + kBoxHalf - 1, kTile);
        const int leadColumn = direction > 0
            ? floorDiv(app.playerX + step + kBoxHalf - 1, kTile)
            : floorDiv(app.playerX + step - kBoxHalf, kTile);
        bool blocked = false;
        for (int row = topRow; row <= bottomRow && !blocked; ++row) {
            blocked = solidTile(tileAt(app, leadColumn, row));
        }
        if (blocked) {
            // FUN_00452CC0 snaps the box edge to the tile boundary.
            if (direction > 0) {
                app.playerX = (floorDiv(app.playerX + kBoxHalf - 1, kTile) + 1) * kTile -
                              kBoxHalf;
            } else {
                app.playerX =
                    floorDiv(app.playerX - kBoxHalf, kTile) * kTile + kBoxHalf;
            }
        } else {
            app.playerX += step;
        }
    }
    app.playerX =
        std::clamp(app.playerX, kBoxHalf, app.catalog.map.columns * kTile - kBoxHalf);
    const int boxLeftColumn = floorDiv(app.playerX - kBoxHalf, kTile);
    const int boxRightColumn = floorDiv(app.playerX + kBoxHalf - 1, kTile);
    const int boxTopRow = floorDiv(static_cast<int>(app.playerY) - kBoxHalf, kTile);
    const int boxBottomRow =
        floorDiv(static_cast<int>(app.playerY) + kBoxHalf - 1, kTile);
    // FUN_00453224 destroys every type-2 tile overlapping the collision box and
    // clears a type-3 goal tile. It runs after horizontal movement and before
    // the standing probe.
    for (int row = boxTopRow; row <= boxBottomRow; ++row) {
        for (int column = boxLeftColumn; column <= boxRightColumn; ++column) {
            if (tileAt(app, column, row) != 2) continue;
            const std::size_t index =
                static_cast<std::size_t>(row) * app.catalog.map.columns + column;
            app.catalog.map.tiles[index] = 0;
            // FUN_00453224 spawns the type-3 popup at the tile centre
            // (tileW*col + 16, tileH*row + 16) and stores the score index in
            // +0x18, which selects the 100/500/1000 frame.
            spawnEntityEffect(app, column * kTile + 16, row * kTile + 16, 3,
                              app.breakableScoreIndex);
            const int values[] = {100, 500, 1000};
            app.score += values[std::min(app.breakableScoreIndex, 2)];
            if (app.breakableScoreIndex < 2) ++app.breakableScoreIndex;
            ++app.breakableCount;
            app.breakStreakTimer = 0x10;
            // FUN_00453224 bleeds 4 energy off the player per broken tile.
            app.jumpEnergy = std::max(0, app.jumpEnergy - 4);
            // FUN_004523b0 index 2 is get.wav (the collection sound).
            playSound("wav/get.wav");
        }
    }
    // FUN_00452774: the standing probe samples the row containing the box's
    // bottom edge with a one-pixel downward bias. Every non-solid column under
    // the box adds 4/32px of gravity; any solid column reports ground contact.
    const int standRow =
        floorDiv(static_cast<int>(app.playerY) + kBoxHalf, kTile);
    const int leftColumn = boxLeftColumn;
    const int rightColumn = boxRightColumn;
    bool groundContact = false;
    int solidColumns = 0;
    for (int column = leftColumn; column <= rightColumn; ++column) {
        if (solidTile(tileAt(app, column, standRow))) {
            groundContact = true;
            ++solidColumns;
        } else {
            app.playerVelocityY += 0.125f;
        }
    }
    if (groundContact) {
        // FUN_00452774 increments +0x40 once per frame when any sampled column
        // is solid, then walks the columns left to right. At most one tile
        // transitions per frame because a transition resets the counter.
        ++app.solidContactFrames;
        for (int column = leftColumn; column <= rightColumn; ++column) {
            if (advanceAnimatedTile(app, column, standRow)) break;
        }
        // Standing contact drains one energy unit per solid column.
        app.jumpEnergy = std::max(0, app.jumpEnergy - solidColumns);
    }
    app.grounded = groundContact;
    if (groundContact && !wasGrounded) {
        // FUN_00452774 resets the animation only on the 0->1 contact edge.
        app.playerAnimTick = 0;
        app.playerSpriteIndex = 0;
        app.playerAnimFrame = 0;
        app.playerFrame = 0;
    }
    // FUN_00453B64 clamps downward velocity to 0xC0 (6px) before jumping.
    app.playerVelocityY = std::min(6.0f, app.playerVelocityY);
    // The original jump is an energy-driven chain jump: it is allowed in the
    // air, each press spending 10 energy and weakening the impulse.
    if (app.jump && app.jumpEnergy < 0x1e) {
        app.score += 0x32;
        if (app.jumpEnergy < 0x15) {
            app.playerVelocityY = static_cast<float>(app.jumpEnergy / 10 - 8);
            app.jumpEnergy += 10;
        } else {
            app.playerVelocityY = -2.0f;
            app.jumpEnergy = 0x1e;
        }
        playSound("wav/jump.wav");
        // FUN_00453B64 spawns a type-4 animated puff when the jump starts with
        // a single contact column.
        if (solidColumns == 1) {
            spawnEntityEffect(app, app.playerX, static_cast<int>(app.playerY), 4, 0);
        }
    }
    app.jump = false;
    // FUN_00453058 / FUN_00452DA4: test the leading edge at the predicted
    // centre, then snap the entity to the tile boundary on contact.
    const int predictedY = static_cast<int>(app.playerY + app.playerVelocityY);
    const int testRow = app.playerVelocityY < 0.0f
        ? floorDiv(predictedY - kBoxHalf, kTile)
        : floorDiv(predictedY + kBoxHalf - 1, kTile);
    bool verticalContact = false;
    for (int column = leftColumn; column <= rightColumn && !verticalContact; ++column) {
        verticalContact = solidTile(tileAt(app, column, testRow));
    }
    if (verticalContact) {
        const int centreY = static_cast<int>(app.playerY);
        if (app.playerVelocityY < 0.0f) {
            const int snapped =
                floorDiv(centreY - kBoxHalf, kTile) * kTile + kBoxHalf;
            app.playerVelocityY = static_cast<float>(snapped - centreY);
        } else {
            const int snapped =
                (floorDiv(centreY + kBoxHalf - 1, kTile) + 1) * kTile - kBoxHalf;
            app.playerVelocityY = static_cast<float>(snapped - centreY);
        }
    }
    app.playerY += app.playerVelocityY;
    // FUN_00453B64 enters the miss state once the entity's screen Y passes
    // 0x100 = 256px, i.e. the player falls out of the bottom of the view. With
    // the up-only camera this is the real "fell too far" test (a world-Y test
    // would never trigger because the bottom floor stops the player).
    // FUN_00454214 then hides the player's render link; the game-over overlay
    // renders over the frozen field. There is no lives pool.
    const int playerScreenY = static_cast<int>(app.playerY) - app.cameraY;
    if (playerScreenY > 0x100) {
        app.playerDead = true;
        app.playerVelocityY = 0.0f;
        app.stateMachine->setScore(app.score);
        app.stateMachine->enterMiss();
        return;
    }
    const int firstGoalColumn = std::max(0, floorDiv(app.playerX - kBoxHalf, kTile));
    const int lastGoalColumn = std::min(
        app.catalog.map.columns - 1,
        floorDiv(app.playerX + kBoxHalf - 1, kTile));
    const int firstGoalRow =
        std::max(0, floorDiv(static_cast<int>(app.playerY) - kBoxHalf, kTile));
    const int lastGoalRow = std::min(
        app.catalog.map.rows - 1,
        floorDiv(static_cast<int>(app.playerY) + kBoxHalf - 1, kTile));
    bool reachedGoal = false;
    for (int row = firstGoalRow; row <= lastGoalRow && !reachedGoal; ++row) {
        for (int column = firstGoalColumn; column <= lastGoalColumn; ++column) {
            if (tileAt(app, column, row) == 3) {
                // FUN_00453224 clears the goal tile when it is touched.
                app.catalog.map.tiles[
                    static_cast<std::size_t>(row) * app.catalog.map.columns + column] = 0;
                reachedGoal = true;
                break;
            }
        }
    }
    if (reachedGoal) {
        app.cleared = true;
        if (app.stateMachine) {
            app.stateMachine->setScore(app.score);
            // FUN_00454DB8 computes the bonus as breakables*100*difficulty
            // plus 100 per surviving time unit.
            app.stateMachine->setBonus(
                app.breakableCount * 100 * app.stateMachine->difficulty() +
                app.timeRemaining * 100);
            app.stateMachine->enterClear();
        }
        playSound("wav/get.wav");
    }
    if (app.breakStreakTimer < 1) {
        app.breakableScoreIndex = 0;
    } else {
        --app.breakStreakTimer;
    }
    // FUN_00453B64 keeps the entity in screen space and scrolls the camera
    // only once the player leaves the [0x40,0xC0] = [64,192] horizontal
    // dead-zone. The vertical camera follows only while the player is inside
    // the top 0x40 pixels and rising; it never scrolls back down (the stage is
    // climbed upward from the bottom shaft).
    const int maxCameraX = std::max(0, app.catalog.map.columns * kTile - kWidth);
    const int maxCameraY = std::max(0, app.catalog.map.rows * kTile - kHeight);
    const int screenX = app.playerX - app.cameraX;
    if (screenX < 0x40) {
        app.cameraX = app.playerX - 0x40;
    } else if (screenX > 0xc0) {
        app.cameraX = app.playerX - 0xc0;
    }
    app.cameraX = std::clamp(app.cameraX, 0, maxCameraX);
    const int screenY = static_cast<int>(app.playerY) - app.cameraY;
    if (screenY < 0x40 && app.playerVelocityY < 0.0f) {
        app.cameraY = static_cast<int>(app.playerY) - 0x40;
    }
    app.cameraY = std::clamp(app.cameraY, 0, maxCameraY);
    // FUN_00450a5c advances the layer-1 accumulator at one eighth of the
    // layer-0 scroll rate (the camera offset is the layer-0 scroll in pixels).
    if (app.backgroundReseed) {
        app.backgroundScrollX = kBackgroundScrollSeed;
        app.backgroundScrollY = 0;
        app.backgroundReseed = false;
    } else {
        app.backgroundScrollX += ((app.cameraX - previousCameraX) * 32) >> 3;
        app.backgroundScrollY += ((app.cameraY - previousCameraY) * 32) >> 3;
    }
    int activeChunks = 0;
    int activePopups = 0;
    int activePuffs = 0;
    for (const bgw::EntityRecord& entity : app.entityPool.slots()) {
        if (entity.active == 0) continue;
        if (entity.type == 2) ++activeChunks;
        else if (entity.type == 3) ++activePopups;
        else if (entity.type == 4) ++activePuffs;
    }
    debugLog("frame x=%d y=%.3f vy=%.3f jumpEnergy=%d grounded=%d cam=(%d,%d) screenY=%d anim=%d/%d dir=%d chunks=%d popups=%d puffs=%d state=%d time=%d/%d",
             app.playerX, app.playerY, app.playerVelocityY, app.jumpEnergy,
             app.grounded ? 1 : 0, app.cameraX, app.cameraY, screenY,
             app.playerSpriteIndex, app.playerAnimFrame, app.playerDirection,
             activeChunks, activePopups, activePuffs,
             app.stateMachine ? static_cast<int>(app.stateMachine->state()) : -1,
             app.timeRemaining, app.timeAccumulator);
    // Keep the state machine's copy used by FUN_0045720C in sync with the
    // live score.
    app.stateMachine->setScore(app.score);
}

void startNewGame(App& app) {
    debugLog("startNewGame");
    if (app.stateMachine) app.stateMachine->startGameplay();
    app.playing = true;
    app.gameOver = false;
    app.scoreRecorded = false;
    app.cleared = false;
    app.playerDead = false;
    app.score = 0;
    app.jumpEnergy = 0;
    app.timeRemaining = 99;
    app.timeAccumulator = 0;
    app.playerX = kSpawnX;
    app.playerY = kSpawnY;
    app.playerVelocityY = 0.0f;
    app.catalog.map.tiles = app.initialTiles;
    if (app.stateMachine) {
        app.catalog.applyLevelRemap(app.stateMachine->difficulty());
    }
    app.breakableScoreIndex = 0;
    app.breakStreakTimer = 0;
    app.breakableCount = 0;
    app.solidContactFrames = 0;
    app.entityPool.reset();
    app.cameraX = kSpawnCameraX;
    app.cameraY = kSpawnCameraY;
    app.backgroundScrollX = kBackgroundScrollSeed;
    app.backgroundScrollY = 0;
    app.backgroundReseed = false;
    app.playerAnimTick = 0;
    app.playerSpriteIndex = 0;
    app.playerAnimFrame = 0;
    app.playerFrame = 0;
    app.bonusAnimation = 0;
    app.wasPostClear = false;
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = appFrom(window);
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        return TRUE;
    case WM_KEYDOWN: {
        bgw::GameStateMachine* sm = app->stateMachine;
        debugLog("WM_KEYDOWN vk=0x%02x playing=%d jumpBefore=%d",
                 static_cast<unsigned>(wParam), app->playing ? 1 : 0, app->jump ? 1 : 0);
        if (!app->playing) {
            if (sm) {
                switch (wParam) {
                case VK_UP: sm->menuMove(-1); break;
                case VK_DOWN: sm->menuMove(1); break;
                case VK_LEFT: sm->menuHorizontal(-1); break;
                case VK_RIGHT: sm->menuHorizontal(1); break;
                // FUN_0045552C activates on button 0 only; on the keyboard
                // that is Space (Z/X are aliases used by the same mapping).
                // Return does nothing in the shipped menu.
                case VK_SPACE:
                case 'Z':
                case 'X': {
                    const bgw::MenuAction action = sm->menuActivate();
                    if (action == bgw::MenuAction::Start) {
                        startNewGame(*app);
                    } else if (action == bgw::MenuAction::Exit) {
                        PostMessage(app->window, WM_CLOSE, 0, 0);
                    }
                    break;
                }
                case VK_ESCAPE: sm->menuBack(); break;
                default: break;
                }
            }
            return 0;
        }
        if (wParam == VK_LEFT) app->left = true;
        if (wParam == VK_RIGHT) app->right = true;
        if (wParam == VK_CONTROL || wParam == VK_SPACE) app->jump = true;
        if (wParam == VK_ESCAPE) {
            if (sm) sm->onKeyDown(0x1b, 0);
            app->playing = false;
        }
        return 0;
    }
    case WM_KEYUP:
        if (wParam == VK_LEFT) app->left = false;
        if (wParam == VK_RIGHT) app->right = false;
        return 0;
    case WM_CHAR:
        if (app->stateMachine && app->stateMachine->state() == bgw::GameState::Rank) {
            if (wParam == VK_BACK) {
                if (!app->rankName.empty()) app->rankName.pop_back();
            } else if (wParam >= 'A' && wParam <= 'Z' && app->rankName.size() < 8) {
                app->rankName.push_back(static_cast<char>(wParam));
            } else if (wParam >= 'a' && wParam <= 'z' && app->rankName.size() < 8) {
                app->rankName.push_back(static_cast<char>(wParam - ('a' - 'A')));
            }
            return 0;
        }
        return 0;
    case WM_TIMER:
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, 0, 0, client.right, client.bottom, app->backDc, 0, 0,
                   kWidth, kHeight, SRCCOPY);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

namespace {
// The shipped assets live alongside the original game, not next to this
// executable, so locate whichever of the working directory, the executable
// directory, or its parent actually contains data\stage.dat and run from there.
void resolveAssetRoot() {
    char moduleName[MAX_PATH]{};
    GetModuleFileNameA(nullptr, moduleName, MAX_PATH);
    std::string exeDir(moduleName);
    const std::size_t slash = exeDir.find_last_of("\\/");
    if (slash != std::string::npos) exeDir.erase(slash);

    const std::string candidates[] = {".", exeDir, exeDir + "\\.."};
    for (const std::string& root : candidates) {
        const std::string probe = root + "\\data\\stage.dat";
        if (GetFileAttributesA(probe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SetCurrentDirectoryA(root.c_str());
            return;
        }
    }
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int showCommand) {
    int stateOverride = -1;
    int rankOverride = -1;
    int nameLengthOverride = 0;
    int timeOverride = -1;
    int spawnXOverride = -1;
    int spawnYOverride = -1;
    if (commandLine && std::strstr(commandLine, "--log")) gDebugEnabled = true;
    if (commandLine && std::strstr(commandLine, "--fast-time")) gFastTimer = true;
    if (commandLine) {
        const char* shot = std::strstr(commandLine, "--shot=");
        if (shot) {
            shot += 7;
            const char* end = shot;
            while (*end && *end != ' ') ++end;
            gShotDir.assign(shot, end);
            if (!gShotDir.empty()) CreateDirectoryA(gShotDir.c_str(), nullptr);
        }
        const char* stateArg = std::strstr(commandLine, "--state=");
        if (stateArg) stateOverride = std::atoi(stateArg + 8);
        const char* timeArg = std::strstr(commandLine, "--time=");
        if (timeArg) timeOverride = std::atoi(timeArg + 7);
        const char* spawnXArg = std::strstr(commandLine, "--spawnx=");
        if (spawnXArg) spawnXOverride = std::atoi(spawnXArg + 9);
        const char* spawnYArg = std::strstr(commandLine, "--spawny=");
        if (spawnYArg) spawnYOverride = std::atoi(spawnYArg + 9);
        const char* shotEvery = std::strstr(commandLine, "--shot-every=");
        if (shotEvery) {
            const int interval = std::atoi(shotEvery + 13);
            if (interval > 0) gShotInterval = interval;
        }
        const char* rankArg = std::strstr(commandLine, "--rank=");
        if (rankArg) {
            rankOverride = std::atoi(rankArg + 7);
            const char* comma = std::strchr(rankArg + 7, ',');
            if (comma) nameLengthOverride = std::atoi(comma + 1);
        }
    }
    debugLog("startup argc=%s", commandLine ? commandLine : "(null)");
    try {
        resolveAssetRoot();
        App app;
        app.catalog = bgw::AssetCatalog::load(
            "data/000spr.dat", "data/001spr.dat", "data/001surf.dat",
            "data/stage.dat", "map/000mpi", "map/000.map");
        loadScores(app);
        const int gameLevel =
            std::clamp(readIniInt("game.ini", "env", "gamelv", 1), 1, 3);
        // Keep the pristine map so every new game can re-apply the difficulty
        // remap using the option menu's current GAME LEVEL. The original
        // reloads the map in FUN_004562F4 and remaps timed walls in
        // FUN_00456D92 each time a game starts, so changing EASY/NORMAL/HARD
        // in the option page takes effect on the next run without a restart.
        app.initialTiles = app.catalog.map.tiles;
        app.title = loadBitmap("bmp/title.bmp");
        // Level 1 loads two map layers (stage.dat: maps = [0, 1, -1]): layer 0
        // is the tiled terrain (map\000.map, atlas bg001.bmp) and layer 1 is a
        // full-screen 1x1 tile (map\001.map) whose atlas is bg002.bmp. See
        // FUN_00454535 (render order layer 1 then layer 0) and FUN_00456bb4.
        app.background = loadBitmap("bmp/bg002.bmp");
        app.options = loadBitmap("bmp/bg002.bmp");
        app.player = loadBitmap("bmp/chr001.bmp");
        app.power = loadBitmap("bmp/chr003.bmp");
        app.tiles = loadBitmap("bmp/bg001.bmp");
        app.collapse = loadBitmap("bmp/chr002.bmp");
        app.scorePopup = loadBitmap("bmp/chr004.bmp");
        app.jumpPuff = loadBitmap("bmp/chr005.bmp");
        app.font = loadBitmap("bmp/font8x8-01.BMP");

        // Reuse the original bgw.exe icon (RT_GROUP_ICON "MAINICON", 32x32).
        // The resource is embedded from src\bgw.rc; load large and small
        // variants so the title bar, taskbar, and Alt+Tab all match.
        HICON appIcon = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
            LR_DEFAULTSIZE | LR_SHARED));
        HICON appIconSmall = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_SHARED));
        WNDCLASSW windowClass{};
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.lpszClassName = L"BGWNativeReconstruction";
        windowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        windowClass.hIcon = appIcon;
        RegisterClassW(&windowClass);

        RECT windowRect{0, 0, kWidth * 2, kHeight * 2};
        AdjustWindowRect(&windowRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        app.window = CreateWindowExW(0, windowClass.lpszClassName, L"BGW Native Reconstruction",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                     CW_USEDEFAULT, CW_USEDEFAULT,
                                     windowRect.right - windowRect.left,
                                     windowRect.bottom - windowRect.top,
                                     nullptr, nullptr, instance, &app);
        if (!app.window) throw std::runtime_error("CreateWindowEx failed");
        if (appIcon) {
            SendMessageW(app.window, WM_SETICON, ICON_BIG,
                         reinterpret_cast<LPARAM>(appIcon));
        }
        if (appIconSmall) {
            SendMessageW(app.window, WM_SETICON, ICON_SMALL,
                         reinterpret_cast<LPARAM>(appIconSmall));
        }

        HDC windowDc = GetDC(app.window);
        app.backDc = CreateCompatibleDC(windowDc);
        app.backBitmap = CreateCompatibleBitmap(windowDc, kWidth, kHeight);
        SelectObject(app.backDc, app.backBitmap);
        ReleaseDC(app.window, windowDc);
        Win32Runtime runtime(app);
        bgw::GameStateMachine stateMachine(runtime);
        app.stateMachine = &stateMachine;
        stateMachine.setSettings(
            gameLevel, readIniBool("game.ini", "env", "waitvsync", false),
            readIniBool("game.ini", "env", "softvsync", false));
        const int jumpButton =
            readIniInt("game.ini", "button", "button1", 0);
        stateMachine.setJumpButton(jumpButton);
        // The load routine (VA 0x455E80) reads button1/button2 and debugflg
        // from game.ini; FormDestroy writes them back as shot/dash and
        // debugflg. Keep the loaded values so a graceful close round-trips.
        app.buttonShot = jumpButton;
        app.buttonDash = readIniInt("game.ini", "button", "button2", 1);
        app.debugFlag = readIniBool("game.ini", "env", "debugflg", false);
        if (stateOverride >= 2) {
            // Test hook: jump straight to a state after seeding a normal game.
            startNewGame(app);
            if (timeOverride >= 0) app.timeRemaining = timeOverride;
            // Test hook: place the player exactly where a fixture needs it so
            // item and wall behaviour can be driven without live input.
            if (spawnXOverride >= 0) app.playerX = spawnXOverride;
            if (spawnYOverride >= 0)
                app.playerY = static_cast<float>(spawnYOverride);
            if (stateOverride == 6 || stateOverride == 7) {
                stateMachine.setRankEditForTest(
                    std::max(0, rankOverride), std::clamp(nameLengthOverride, 0, 3));
            }
            if (stateOverride == 10) {
                // Entering state 10 normally initialises its tally from the
                // breakable count, difficulty, and surviving time.
                stateMachine.setBonus(
                    app.breakableCount * 100 * stateMachine.difficulty() +
                    app.timeRemaining * 100);
            }
            stateMachine.forceState(
                static_cast<bgw::GameState>(stateOverride));
        }
        drawFrame(app);
        ShowWindow(app.window, showCommand);
        // The shipped FUN_00454535 busy-waits 16 ms only when SOFTWARE VSYNC
        // is enabled (+0x32c) and otherwise free-runs, reaching ~125 fps on
        // the reference display. That made the per-frame physics run at double
        // speed, so the reconstruction now locks the whole loop to 60 Hz. The
        // 16/17/17 ms repeat averages exactly 16.667 ms (60 fps); SetTimer is
        // unreliable here (Windows coalesces it to ~35 ticks/s), so run an
        // explicit loop with 1 ms timer resolution.
        timeBeginPeriod(1);

        DWORD nextFrame = timeGetTime();
        unsigned framePhase = 0;
        MSG message{};
        bool running = true;
        while (running) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) break;
            const DWORD frameMs = 16 + ((framePhase++ % 3) != 0 ? 1 : 0);
            const DWORD now = timeGetTime();
            if (static_cast<int>(now - nextFrame) >= 0) {
                nextFrame += frameMs;
                if (now - nextFrame > 200) nextFrame = now + frameMs;
                if (app.stateMachine) app.stateMachine->tick(app.runtimeObject);
                if (app.stateMachine) {
                    const bgw::GameState state = app.stateMachine->state();
                    app.playing =
                        state == bgw::GameState::Gameplay ||
                        state == bgw::GameState::LevelIntro ||
                        state == bgw::GameState::Miss ||
                        state == bgw::GameState::GameOver ||
                        state == bgw::GameState::Clear ||
                        state == bgw::GameState::PostClear;
                }
                update(app);
                drawFrame(app);
                static int shotCounter = 0;
                if (!gShotDir.empty() && (shotCounter++ % gShotInterval) == 0) {
                    saveBackBuffer(app.backDc, kWidth, kHeight);
                }
                HDC dc = GetDC(app.window);
                RECT client{};
                GetClientRect(app.window, &client);
                SetStretchBltMode(dc, COLORONCOLOR);
                StretchBlt(dc, 0, 0, client.right, client.bottom, app.backDc, 0, 0,
                           kWidth, kHeight, SRCCOPY);
                ReleaseDC(app.window, dc);
            } else {
                const DWORD remaining = nextFrame - now;
                Sleep(remaining > 2 ? remaining - 1 : 0);
            }
        }
        timeEndPeriod(1);
        // The shipped FormDestroy (VA 0x456F7C) rewrites game.ini and
        // data\rank.dat only on a graceful close, so an aborted name entry or
        // an option change during a crash is lost. Match that order.
        writeIniSettings(app);
        writeRankFile(app);
        DeleteDC(app.backDc);
        DeleteObject(app.backBitmap);
        DeleteObject(app.title);
        DeleteObject(app.background);
        DeleteObject(app.options);
        DeleteObject(app.player);
        DeleteObject(app.power);
        DeleteObject(app.tiles);
        DeleteObject(app.font);
        return 0;
    } catch (const std::exception& error) {
        char cwd[MAX_PATH]{};
        GetCurrentDirectoryA(MAX_PATH, cwd);
        const std::string message = std::string(error.what()) +
            "\n\nWorking directory: " + cwd +
            "\n\nRun this executable from the folder that contains data\\, map\\,"
            " bmp\\ and game.ini (the shipped game folder).";
        MessageBoxA(nullptr, message.c_str(), "BGW native build failed",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
}
