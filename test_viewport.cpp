#include "globals.h"
void test() {
    tft.native()->setViewport(0, 0, 10, 10);
    tft.native()->resetViewport();
}
