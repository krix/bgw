#include "game_state.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace bgw {

namespace {
constexpr int kEscape = 0x1b;
constexpr int kReturnToMenu = 0x52; // 'R' in the original handler.
constexpr int kAlphabetLength = 30;
}

GameStateMachine::GameStateMachine(RuntimeApi& runtime) : runtime_(runtime) {}

void GameStateMachine::tick(GameObjectState& object) {
    // FUN_00454535 samples the input state at the end of a frame; the state
    // handlers below consume the freshly sampled buttons. Doing it up front
    // keeps edge detection self-contained without changing observable frames.
    pollInput();
    switch (state_) {
    case GameState::MenuInit:  tickMenuInit(); break;
    case GameState::Menu:      tickMenu(); break;
    case GameState::Gameplay:  tickGameplay(); break;
    case GameState::LevelIntro:tickLevelIntro(); break;
    case GameState::Miss:      tickMiss(); break;
    case GameState::GameOver:  tickGameOver(); break;
    case GameState::Rank:      tickRank(); break;
    case GameState::Result:    tickResult(); break;
    case GameState::Continue:  tickContinue(); break;
    case GameState::Clear:     tickClear(); break;
    case GameState::PostClear: tickPostClear(); break;
    case GameState::ExitDialog:
        // FUN_004524AC draws a centered "EXIT ESC KEY" in style 2. The shipped
        // binary makes state 99 unreachable (ESC goes straight to the menu),
        // so the renderer owns this screen rather than the state machine.
        break;
    }

    runtime_.updateSprites();
    runtime_.present();
    finishFrame(object);
}

void GameStateMachine::tickMenuInit() {
    runtime_.stopMusic();
    runtime_.playMidi("midi\\init", false);
    frameCounter_ = 0;
    menuPage_ = 0;
    menuIndex_ = 0;
    state_ = GameState::Menu;
}

void GameStateMachine::tickMenu() {
    // FUN_0045552C owns the title/menu interaction. Its state 2 result starts
    // the game; the actual drawing and input are driven by the Win32 layer.
}

void GameStateMachine::setSettings(int32_t level, bool wait, bool vsync) {
    difficulty_ = std::clamp(level, 1, 3);
    waitEnabled_ = wait;
    vsyncEnabled_ = vsync;
}

void GameStateMachine::setJumpButton(int button) {
    jumpButton_ = std::clamp(button, 0, 15);
}

void GameStateMachine::tickGameplay() {
    // FUN_00454535 state 2 is the short level banner. Its text is drawn by the
    // renderer (drawFrame) after the map and HUD; this routine only owns the
    // 0x78-frame lifetime. State 3 is the normal play state.
    if (state_ == GameState::Gameplay) {
        ++frameCounter_;
        if (frameCounter_ > 0x78) {
            frameCounter_ = 0;
            state_ = GameState::LevelIntro;
        }
    }
    runtime_.updateGameObjects();
}

void GameStateMachine::tickLevelIntro() {
    // State 3 has no independent transition in the dispatch routine; it stays
    // in the shared gameplay update until the game logic selects another
    // state.
    runtime_.updateGameObjects();
}

void GameStateMachine::tickMiss() {
    // FUN_00454535 state 4: start the miss track once, wait for it to finish,
    // then move to the GAME OVER overlay.
    if (frameCounter_ == 0) {
        runtime_.stopMusic();
        runtime_.playMidi("midi\\miss", false);
    }
    ++frameCounter_;
    if (runtime_.musicFinished()) {
        frameCounter_ = 0;
        state_ = GameState::GameOver;
    }
}

void GameStateMachine::tickGameOver() {
    // FUN_00454535 state 5: the "GAME  OVER" overlay stays up for the length
    // of midi\gameover. FUN_0045720C then inserts the score and picks the
    // name-entry screen (state 6) or the menu (state 0).
    if (frameCounter_ == 0) {
        runtime_.stopMusic();
        runtime_.playMidi("midi\\gameover", false);
    }
    ++frameCounter_;
    if (runtime_.musicFinished()) {
        frameCounter_ = 0;
        const int rank = runtime_.insertRankScore(score_);
        if (rank >= 0) {
            rankIndex_ = rank;
            nameCursor_ = 1;
            nameLength_ = 0;
            state_ = GameState::Rank;
        } else {
            state_ = GameState::MenuInit;
        }
    }
}

void GameStateMachine::tickRank() {
    // FUN_0045729C. Vertical input moves the one-based alphabet cursor,
    // button 0 accepts the current character. The routine reports completion
    // once three characters have been written.
    if (input_.upEdge) {
        nameCursor_ = nameCursor_ <= 1 ? kAlphabetLength : nameCursor_ - 1;
    }
    if (input_.downEdge) {
        nameCursor_ = nameCursor_ >= kAlphabetLength ? 1 : nameCursor_ + 1;
    }
    if (input_.buttonEdge && nameLength_ < 3) {
        runtime_.writeRankNameByte(rankIndex_, nameLength_, currentNameChar());
        ++nameLength_;
    }
    if (nameLength_ > 2) {
        frameCounter_ = 0;
        state_ = GameState::Result;
    }
}

void GameStateMachine::tickResult() {
    // FUN_00454535 state 7: show the completed ranking table until a button
    // edge or the 600-frame timeout returns to the menu.
    ++frameCounter_;
    if (input_.buttonEdge || input_.jumpEdge || frameCounter_ > 600) {
        frameCounter_ = 0;
        state_ = GameState::MenuInit;
    }
}

void GameStateMachine::tickContinue() {
    // FUN_00454535 state 8: a 9-second continue countdown. The displayed value
    // is 9 - frameCounter / 0x78. Button 0 fast-forwards the countdown; the
    // jump/second button resumes gameplay; letting it expire shows the ranking.
    ++frameCounter_;
    if (input_.buttonEdge) {
        frameCounter_ = (frameCounter_ / 0x78) * 0x78 + 0x78;
    }
    if (input_.jumpEdge) {
        frameCounter_ = 0;
        state_ = GameState::Gameplay;
    }
    if (frameCounter_ > 0x4af) {
        frameCounter_ = 0;
        state_ = GameState::Result;
    }
}

void GameStateMachine::tickClear() {
    // State 9 waits for the clear track before entering the scoreboard path.
    ++frameCounter_;
    if (runtime_.musicFinished()) {
        frameCounter_ = 0;
        state_ = GameState::PostClear;
    }
}

void GameStateMachine::tickPostClear() {
    // FUN_00454DB8: phase 1 waits 0x78 frames; phase 2 tallies the recovered
    // level bonus into the score 100 points per frame; phase 3 waits 0x4b0
    // frames before inserting the score. The three blocks are sequential in
    // the original, so the frame that ends phase 1 also performs the first
    // 100-point step and the frame that empties the tally also starts the
    // phase-3 counter at 1.
    if (phase_ == 0) {
        if (++frameCounter_ > 0x78) {
            frameCounter_ = 0;
            phase_ = 1;
        }
    }
    if (phase_ == 1) {
        if (bonus_ > 0) {
            const int32_t tick = std::min(bonus_, 100);
            bonus_ -= tick;
            score_ += tick;
        }
        if (bonus_ <= 0) {
            bonus_ = 0;
            frameCounter_ = 0;
            phase_ = 2;
        }
    }
    if (phase_ == 2) {
        if (++frameCounter_ > 0x4b0) {
            frameCounter_ = 0;
            phase_ = 0;
            pendingBonus_ = 0;
            const int rank = runtime_.insertRankScore(score_);
            if (rank >= 0) {
                rankIndex_ = rank;
                nameCursor_ = 1;
                nameLength_ = 0;
                frameCounter_ = 1;
                state_ = GameState::Rank;
            } else {
                state_ = GameState::MenuInit;
            }
        }
    }
}

void GameStateMachine::pollInput() {
    const bool button = runtime_.keyDown(0x20) || runtime_.keyDown('Z') ||
                        runtime_.keyDown('X');
    const bool jump = runtime_.keyDown(0x11) || runtime_.keyDown(0x20);
    const bool up = runtime_.keyDown(0x26);
    const bool down = runtime_.keyDown(0x28);

    input_.buttonEdge = button && !prevButton_;
    input_.jumpEdge = jump && !prevJump_;
    input_.upEdge = up && !prevUp_;
    input_.downEdge = down && !prevDown_;

    prevButton_ = button;
    prevJump_ = jump;
    prevUp_ = up;
    prevDown_ = down;

    input_.leftPressed = runtime_.keyDown(0x25);
    input_.rightPressed = runtime_.keyDown(0x27);
    input_.upPressed = up;
    input_.downPressed = down;
    input_.buttonPressed = button;
    input_.jumpPressed = jump;
}

void GameStateMachine::menuMove(int delta) {
    if (delta == 0) return;
    if (menuPage_ == 0) {
        menuIndex_ = (menuIndex_ + delta + 3) % 3;
    } else if (menuPage_ == 1) {
        optionIndex_ = (optionIndex_ + delta + 6) % 6;
    }
    // Page 2 (ranking) has no vertical selection.
}

void GameStateMachine::menuHorizontal(int delta) {
    if (delta == 0 || menuPage_ != 1) return;
    switch (optionIndex_) {
    case 0: // GAME LEVEL: clamped to EASY/NOMAL/HARD, no wrap.
        if (delta < 0 && difficulty_ > 1) --difficulty_;
        if (delta > 0 && difficulty_ < 3) ++difficulty_;
        break;
    case 2: // WAIT
        waitEnabled_ = !waitEnabled_;
        break;
    case 3: // SOFTWARE VSYNC
        vsyncEnabled_ = !vsyncEnabled_;
        break;
    default:
        break;
    }
}

void GameStateMachine::menuBack() {
    if (menuPage_ == 2) {
        menuPage_ = 1;
    } else if (menuPage_ == 1) {
        menuPage_ = 0;
    }
}

MenuAction GameStateMachine::menuActivate() {
    if (menuPage_ == 2) {
        // FUN_0045552C page 2: any start button returns to the option list
        // without resetting the selection.
        menuPage_ = 1;
        return MenuAction::None;
    }
    if (menuPage_ == 1) {
        switch (optionIndex_) {
        case 0: // GAME LEVEL is changed with left/right.
        case 1: // JUMP BUTTON ASSIGN captures a button in the input handler.
        case 2: // WAIT is toggled with left/right.
        case 3: // SOFTWARE VSYNC is toggled with left/right.
            break;
        case 4: // RANKING
            menuPage_ = 2;
            break;
        case 5: // EXIT returns to the title with OPTION selected.
            menuPage_ = 0;
            menuIndex_ = 1;
            break;
        default:
            break;
        }
        return MenuAction::None;
    }
    switch (menuIndex_) {
    case 0:
        startGameplay();
        return MenuAction::Start;
    case 1:
        menuPage_ = 1;
        optionIndex_ = 0;
        return MenuAction::None;
    case 2:
        return MenuAction::Exit;
    default:
        return MenuAction::None;
    }
}

void GameStateMachine::onKeyDown(int16_t key, int16_t shift) {
    if (key == kEscape) {
        if (state_ == GameState::ExitDialog) {
            state_ = GameState::MenuInit;
        } else {
            stateBeforeExit_ = state_;
            state_ = GameState::MenuInit;
        }
    }
    if (state_ == GameState::ExitDialog && key != kEscape) {
        state_ = stateBeforeExit_;
    }
    if (key == kReturnToMenu && shift == 0) {
        state_ = GameState::MenuInit;
    }
}

void GameStateMachine::finishFrame(GameObjectState& object) {
    // FUN_00454535 only busy-waits 16 ms when SOFTWARE VSYNC is enabled; with
    // both options off the shipped loop is free running. The Win32 frame loop
    // owns that pacing (8 ms free running / 16 ms with SOFTWARE VSYNC) so the
    // 0x78-frame countdown stays at ~1 second. Keep the timestamp in sync for
    // API compatibility with the original per-frame routine.
    object.lastFrameTime = runtime_.timeGetTime();
}

} // namespace bgw
