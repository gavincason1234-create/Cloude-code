#pragma once
#include <Arduino.h>
#include "display.h"

#define MENU_MAX_ITEMS  16
#define MENU_ROW_H      10   // pixels per row
#define MENU_Y_START    11   // below status bar

struct MenuItem {
    const char* label;
    void        (*action)();   // called on OK
};

class Menu {
public:
    Menu(Display& disp);

    void addItem(const char* label, void (*action)());
    void clear();

    void draw(const char* title = nullptr);
    void moveUp();
    void moveDown();
    void select();           // execute action at cursor
    int  cursor() const { return _cursor; }

private:
    Display&   _disp;
    MenuItem   _items[MENU_MAX_ITEMS];
    int        _count  = 0;
    int        _cursor = 0;
    int        _offset = 0;   // scroll offset

    static const int VISIBLE_ROWS = (OLED_HEIGHT - MENU_Y_START) / MENU_ROW_H;
};
