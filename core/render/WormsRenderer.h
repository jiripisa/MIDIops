#pragma once
namespace core {
class Display;
class NoteWormModel;
namespace WormsRenderer {
    void drawWorms(const NoteWormModel& model, Display& d);
    void drawKeyboard(const NoteWormModel& model, Display& d);
    void render(const NoteWormModel& model, Display& d);  // worms then keyboard
}
} // namespace core
