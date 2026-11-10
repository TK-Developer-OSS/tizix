; blink gpio4 every 500 ms: the timer repeats, intr flips the pin, loop does nothing
setup:
    HI timer_repeat
    TLIM 500
    END
intr:
    LO timer_finish
    IFH gpio4
    JMPF low_it
    HI gpio4
    JMPF done
low_it:
    LO gpio4
done:
    RETI
loop:
    JMPB loop
