# ESP-IDF component manifest for esp32-rtl-sdr (P4 fork, Pilot Kit edition).
#
# Upstream layout was a standalone ESP-IDF example project. We have stripped
# the example app (usb_host_lib_main.c / class_driver.c) and re-register the
# remaining driver sources as a normal IDF component so the parent firmware
# can depend on it via `REQUIRES esp32-rtl-sdr`.

set(SDR_SRCS
    "main/librtlsdr.c"
    "main/esp_libusb.c"
    "main/tuner_e4k.c"
    "main/tuner_fc0012.c"
    "main/tuner_fc0013.c"
    "main/tuner_fc2580.c"
    "main/tuner_r82xx.c"
    "main/mode-s.c"
    "main/demod1090.c"
)

idf_component_register(
    SRCS ${SDR_SRCS}
    INCLUDE_DIRS "main"
    # ESP-IDF v6.0 moved USB host out of core into the registry. The
    # double-underscore form is how the IDF component manager exposes
    # `espressif/usb` to CMake-level REQUIRES.
    REQUIRES espressif__usb
    PRIV_REQUIRES esp_driver_gpio
)

# Upstream relies on these warnings being non-fatal; keep the relaxation so
# we don't have to touch every tuner driver.
set_source_files_properties(
    "main/tuner_r82xx.c"
    PROPERTIES COMPILE_FLAGS
    "-Wno-unused-function -Wno-unused-variable -Wno-incompatible-pointer-types"
)
