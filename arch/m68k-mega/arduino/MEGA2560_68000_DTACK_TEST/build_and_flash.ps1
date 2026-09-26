param(
    [string]$Port = "COM3",
    [switch]$FlashOnly,
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$TOOLS_DIR = "$env:LOCALAPPDATA\Arduino15\packages\arduino\tools\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin"
$AVRDUDE   = "$env:LOCALAPPDATA\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin\avrdude.exe"
$AVRDUDE_CONF = "$env:LOCALAPPDATA\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\etc\avrdude.conf"
$CORE_DIR  = "$env:LOCALAPPDATA\Arduino15\packages\arduino\hardware\avr\1.8.6"

$BUILD_DIR = "$PSScriptRoot\build"

if (-not $FlashOnly) {
    Write-Host "=== 1. Building Mega 2560 Firmware ===" -ForegroundColor Cyan
    if (!(Test-Path $BUILD_DIR)) { New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null }

    $CC  = "$TOOLS_DIR\avr-gcc.exe"
    $CXX = "$TOOLS_DIR\avr-g++.exe"
    $AR  = "$TOOLS_DIR\avr-gcc-ar.exe"
    $OBJCOPY = "$TOOLS_DIR\avr-objcopy.exe"

    # tizix #85: Serial RX buffer 64 -> 256 bytes so that one XMODEM packet (133 B) fits
    # while the 68000 drains it (rx on tizix). Must be the SAME value for the core and the
    # sketch (it changes the HardwareSerial layout), so it goes into both flag sets.
    $DEFS = "-DSERIAL_RX_BUFFER_SIZE=256"
    $CFLAGS = "-c -g -Os -w -std=gnu11 -ffunction-sections -fdata-sections -MMD -flto -fno-fat-lto-objects -mmcu=atmega2560 -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR $DEFS -I`"$CORE_DIR\cores\arduino`" -I`"$CORE_DIR\variants\mega`""
    $CXXFLAGS = "-c -g -Os -w -std=gnu++11 -fpermissive -fno-exceptions -ffunction-sections -fdata-sections -fno-threadsafe-statics -MMD -flto -mmcu=atmega2560 -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR $DEFS -I`"$CORE_DIR\cores\arduino`" -I`"$CORE_DIR\variants\mega`" -I`"$PSScriptRoot`""

    # core.a is cached: rebuild it when the flags change (a stale core with another
    # SERIAL_RX_BUFFER_SIZE would not match the sketch).
    $STAMP = "$BUILD_DIR\core.flags"
    if ((Test-Path "$BUILD_DIR\core.a") -and (!(Test-Path $STAMP) -or ((Get-Content $STAMP -Raw) -ne $CXXFLAGS))) {
        Write-Host "Core flags changed -> rebuilding core.a" -ForegroundColor Yellow
        Remove-Item "$BUILD_DIR\*.o", "$BUILD_DIR\*.d", "$BUILD_DIR\core.a" -ErrorAction SilentlyContinue
    }

    # Compile core files if core.a does not exist
    if (!(Test-Path "$BUILD_DIR\core.a")) {
        Write-Host "Compiling Arduino AVR Core..." -ForegroundColor Yellow
        $cFiles = Get-ChildItem "$CORE_DIR\cores\arduino\*.c"
        foreach ($f in $cFiles) {
            $obj = "$BUILD_DIR\$($f.BaseName).c.o"
            & $CC ($CFLAGS -split ' ') $f.FullName -o $obj
            & $AR rcs "$BUILD_DIR\core.a" $obj
        }
        $cppFiles = Get-ChildItem "$CORE_DIR\cores\arduino\*.cpp"
        foreach ($f in $cppFiles) {
            $obj = "$BUILD_DIR\$($f.BaseName).cpp.o"
            & $CXX ($CXXFLAGS -split ' ') $f.FullName -o $obj
            & $AR rcs "$BUILD_DIR\core.a" $obj
        }
        Set-Content -Path $STAMP -Value $CXXFLAGS -NoNewline -Encoding ascii
    }

    Write-Host "Compiling Sketch..." -ForegroundColor Yellow
    & $CXX ($CXXFLAGS -split ' ') -x c++ "$PSScriptRoot\MEGA2560_68000_DTACK_TEST.ino" -o "$BUILD_DIR\sketch.o"

    Write-Host "Linking..." -ForegroundColor Yellow
    $LDFLAGS = "-w -Os -g -flto -fuse-linker-plugin -Wl,--gc-sections -mmcu=atmega2560 -o `"$BUILD_DIR\firmware.elf`" `"$BUILD_DIR\sketch.o`" `"$BUILD_DIR\core.a`" -L`"$BUILD_DIR`" -lm"
    & $CC ($LDFLAGS -split ' ')

    Write-Host "Creating HEX..." -ForegroundColor Yellow
    & $OBJCOPY -O ihex -R .eeprom "$BUILD_DIR\firmware.elf" "$BUILD_DIR\firmware.hex"
    Write-Host "Build Succeeded: $BUILD_DIR\firmware.hex" -ForegroundColor Green
}

if (-not $BuildOnly) {
    Write-Host "=== 2. Flashing to Mega 2560 on $Port ===" -ForegroundColor Cyan
    & $AVRDUDE -C $AVRDUDE_CONF -v -p atmega2560 -c wiring -P $Port -b 115200 -D -U "flash:w:$BUILD_DIR\firmware.hex:i"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Flash Succeeded!" -ForegroundColor Green
    } else {
        Write-Error "Flash Failed with exit code $LASTEXITCODE"
    }
}
