#pragma once

// wraps what DirectInput8Create handed the game, so input a plugin took can be kept from it
void InputWrap(void* directInput);
void InputCapture(bool mouse, bool keyboard);
