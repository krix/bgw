#pragma once

#include <array>
#include <cstdint>

#include "data_layout.h"

namespace bgw {

enum class GameState : int32_t {
    MenuInit = 0,
    Menu = 1,
    Gameplay = 2,
    LevelIntro = 3,
    Miss = 4,
    GameOver = 5,
    Rank = 6,
    Result = 7,
    Continue = 8,
    Clear = 9,
    PostClear = 10,
    ExitDialog = 99,
};

// FUN_0045552C returns one of these from the title/option menu interaction.
enum class MenuAction { None, Start, Exit };

struct InputState {
    bool leftPressed = false;
    bool rightPressed = false;
    bool upPressed = false;
    bool downPressed = false;
    bool buttonPressed = false;
    bool jumpPressed = false;
    bool buttonEdge = false;
    bool jumpEdge = false;
    bool upEdge = false;
    bool downEdge = false;
    std::array<int32_t, 4> repeatFrames{};
};

struct GameObjectState {
    uint32_t lastFrameTime = 0;
    bool windowedVsync = true;
    bool softVsync = false;
};

class RuntimeApi {
public:
    virtual ~RuntimeApi() = default;

    virtual void stopMusic() = 0;
    virtual void playMidi(const char* name, bool loop) = 0;
    virtual bool musicFinished() = 0;
    virtual void clearFrame() = 0;
    virtual void present() = 0;
    virtual void updateSprites() = 0;
    virtual void updateGameObjects() = 0;
    virtual void drawText(int x, int y, const char* text, int style) = 0;
    // FUN_0045720C: insert a score into the ten-entry table. Returns the
    // zero-based rank it was inserted at, or -1 when it does not beat any
    // entry. The inserted name starts as four zero bytes.
    virtual int insertRankScore(int32_t score) = 0;
    // FUN_0045729C writes the accepted name characters into the four-byte name
    // field of the ranking entry selected by insertRankScore.
    virtual void writeRankNameByte(int32_t rank, int32_t offset, char value) = 0;
    virtual uint32_t timeGetTime() = 0;
    virtual bool keyDown(int keyCode) = 0;
};

class GameStateMachine {
public:
    explicit GameStateMachine(RuntimeApi& runtime);

    void tick(GameObjectState& object);
    void onKeyDown(int16_t key, int16_t shift);
    void startGameplay() {
        state_ = GameState::Gameplay;
        frameCounter_ = 0;
        phase_ = 0;
        score_ = 0;
        bonus_ = 0;
        pendingBonus_ = 0;
    }
    void enterMiss() { state_ = GameState::Miss; frameCounter_ = 0; }
    // Test/support hook used by --state so individual screens can be captured
    // without replaying a full round.
    void forceState(GameState state) {
        state_ = state;
        frameCounter_ = 0;
        phase_ = 0;
    }
    void enterClear() {
        state_ = GameState::Clear;
        frameCounter_ = 0;
        phase_ = 0;
        // FUN_00453224 starts midi\clear on the goal collision; state 9 waits
        // for it to finish before the scoreboard.
        runtime_.stopMusic();
        runtime_.playMidi("midi\\clear", false);
    }
    void setScore(int32_t score) { score_ = score; }
    // FUN_00454DB8 tallies the level bonus into the score 100 points at a time.
    void setBonus(int32_t bonus) { bonus_ = bonus; pendingBonus_ = bonus; }

    GameState state() const { return state_; }
    int32_t frameCounter() const { return frameCounter_; }
    int32_t score() const { return score_; }
    int32_t remainingBonus() const { return bonus_; }
    // FUN_0045720C / FUN_0045729C state: the ranking row being edited, how
    // many characters have been accepted (0..3), and the 1-based cursor into
    // "ABCDEFGHIJKLMNOPQRSTUVWXYZ.!? ".
    int32_t rankIndex() const { return rankIndex_; }
    int32_t nameLength() const { return nameLength_; }
    int32_t nameCursor() const { return nameCursor_; }
    char currentNameChar() const {
        static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.!? ";
        const int index = (nameCursor_ >= 1 && nameCursor_ <= 30) ? nameCursor_ - 1 : 0;
        return kAlphabet[index];
    }
    void setRankEditForTest(int32_t rank, int32_t length) {
        rankIndex_ = rank;
        nameLength_ = length;
    }

    // Title/option menu (FUN_0045552C). Page 0 is GAME START/OPTION/EXIT,
    // page 1 is the option list, page 2 is the ranking page (plus the
    // "BUTTON TO BACK" prompt).
    int menuIndex() const { return menuIndex_; }
    int optionIndex() const { return optionIndex_; }
    int menuPage() const { return menuPage_; }
    // FUN_0045552C stores the game level in 1..3 and indexes the string table
    // PTR_PTR_0045a39c with `level - 1`, yielding EASY/NOMAL/HARD.
    int32_t difficulty() const { return difficulty_; }
    bool waitEnabled() const { return waitEnabled_; }
    bool vsyncEnabled() const { return vsyncEnabled_; }
    int jumpButton() const { return jumpButton_; }
    void setSettings(int32_t level, bool wait, bool vsync);
    void setJumpButton(int button);
    void menuMove(int delta);
    // Horizontal input (left = -1, right = +1) drives GAME LEVEL, WAIT, and
    // SOFTWARE VSYNC; the original reads PTR_DAT_0045a0e4 (x) outside the
    // vertical-wrap guard.
    void menuHorizontal(int delta);
    MenuAction menuActivate();
    void menuBack();

private:
    void tickMenuInit();
    void tickMenu();
    void tickGameplay();
    void tickLevelIntro();
    void tickMiss();
    void tickGameOver();
    void tickRank();
    void tickResult();
    void tickContinue();
    void tickClear();
    void tickPostClear();
    void pollInput();
    void finishFrame(GameObjectState& object);

    RuntimeApi& runtime_;
    GameState state_ = GameState::MenuInit;
    GameState stateBeforeExit_ = GameState::Menu;
    InputState input_;
    int32_t frameCounter_ = 0;
    int32_t difficulty_ = 1;
    int32_t score_ = 0;
    int32_t bonus_ = 0;
    int32_t pendingBonus_ = 0;
    int32_t phase_ = 0;
    int menuIndex_ = 0;
    int optionIndex_ = 0;
    int menuPage_ = 0;
    int jumpButton_ = 0;
    bool waitEnabled_ = false;
    bool vsyncEnabled_ = false;
    int32_t rankIndex_ = 0;
    int32_t nameCursor_ = 1;
    int32_t nameLength_ = 0;
    bool prevButton_ = false;
    bool prevJump_ = false;
    bool prevUp_ = false;
    bool prevDown_ = false;
};

} // namespace bgw
