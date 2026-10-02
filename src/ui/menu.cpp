#include "menu.h"

Menu::Menu(Display& disp) : _disp(disp) {}

void Menu::addItem(const char* label, void (*action)()) {
    if (_count >= MENU_MAX_ITEMS) return;
    _items[_count++] = {label, action};
}

void Menu::clear() {
    _count = _cursor = _offset = 0;
}

void Menu::draw(const char* title) {
    _disp.clear();

    if (title) {
        _disp.drawStatusBar(title, "");
    }

    for (int i = 0; i < VISIBLE_ROWS && (_offset + i) < _count; i++) {
        int idx = _offset + i;
        int y   = MENU_Y_START + i * MENU_ROW_H;
        bool selected = (idx == _cursor);

        if (selected) {
            _disp.fillRect(0, y, OLED_WIDTH, MENU_ROW_H);
            _disp.print(2, y + 1, _items[idx].label, 1, true);
        } else {
            _disp.print(2, y + 1, _items[idx].label);
        }
    }

    // Scroll indicator
    if (_count > VISIBLE_ROWS) {
        int barH   = OLED_HEIGHT / _count;
        int barTop = MENU_Y_START + (_cursor * (OLED_HEIGHT - MENU_Y_START)) / _count;
        _disp.fillRect(OLED_WIDTH - 2, barTop, 2, max(barH, 2));
    }

    _disp.show();
}

void Menu::moveUp() {
    if (_cursor > 0) {
        _cursor--;
        if (_cursor < _offset) _offset = _cursor;
    }
}

void Menu::moveDown() {
    if (_cursor < _count - 1) {
        _cursor++;
        if (_cursor >= _offset + VISIBLE_ROWS) _offset = _cursor - VISIBLE_ROWS + 1;
    }
}

void Menu::select() {
    if (_cursor < _count && _items[_cursor].action) {
        _items[_cursor].action();
    }
}
