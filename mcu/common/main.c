#include "mcu.h"

int main(void)
{
    hal_init();
    shell_run();
    return 0;
}
