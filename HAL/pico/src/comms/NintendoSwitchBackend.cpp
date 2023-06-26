#include "comms/NintendoSwitchBackend.hpp"

#include "core/CommunicationBackend.hpp"
#include "core/state.hpp"

#include <Adafruit_TinyUSB.h>
#include <TUCompositeHID.hpp>
#include <math.h>

#define SIGNUM(x) ((x > 0) - (x < 0))

// clang-format off

#define HID_REPORT_DESC(...) \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )                 ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_GAMEPAD  )                 ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                 ,\
        /* Report ID if any */\
        __VA_ARGS__ \
        /* 16 bit Button Map */ \
        HID_LOGICAL_MIN    ( 0                                      ) ,\
        HID_LOGICAL_MAX    ( 1                                      ) ,\
        HID_PHYSICAL_MIN   ( 0                                      ) ,\
        HID_PHYSICAL_MAX   ( 1                                      ) ,\
        HID_REPORT_SIZE    ( 1                                      ) ,\
        HID_REPORT_COUNT   ( 16                                     ) ,\
        HID_USAGE_PAGE     ( HID_USAGE_PAGE_BUTTON                  ) ,\
        HID_USAGE_MIN      ( 1                                      ) ,\
        HID_USAGE_MAX      ( 16                                     ) ,\
        HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        /* 8 bit DPad/Hat */ \
        HID_USAGE_PAGE     ( HID_USAGE_PAGE_DESKTOP                 ) ,\
        HID_LOGICAL_MAX    ( 7                                      ) ,\
        HID_PHYSICAL_MAX_N ( 315, 2                                 ) ,\
        HID_REPORT_SIZE    ( 4                                      ) ,\
        HID_REPORT_COUNT   ( 1                                      ) ,\
        HID_UNIT           ( 0x14                                   ) ,\
        HID_USAGE          ( HID_USAGE_DESKTOP_HAT_SWITCH           ) ,\
        HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NULL_STATE ) ,\
        /* Useless padding after hat switch */ \
        HID_UNIT           ( 0x00                                   ) ,\
        HID_REPORT_COUNT   ( 1                                      ) ,\
        HID_INPUT          ( HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE ) ,\
        /* 8 bit analog stick axes, range 0-255 */ \
        HID_LOGICAL_MAX_N  ( 255, 2                                 ) ,\
        HID_PHYSICAL_MAX_N ( 255, 2                                 ) ,\
        HID_USAGE          ( HID_USAGE_DESKTOP_X                    ) ,\
        HID_USAGE          ( HID_USAGE_DESKTOP_Y                    ) ,\
        HID_USAGE          ( HID_USAGE_DESKTOP_Z                    ) ,\
        HID_USAGE          ( HID_USAGE_DESKTOP_RZ                   ) ,\
        HID_REPORT_SIZE    ( 8                                      ) ,\
        HID_REPORT_COUNT   ( 4                                      ) ,\
        HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        /* Some random vendor input idk */ \
        HID_USAGE_PAGE_N   ( HID_USAGE_PAGE_VENDOR, 2               ) ,\
        HID_USAGE          ( 0x20                                   ) ,\
        HID_REPORT_COUNT   ( 1                                      ) ,\
        HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        /* Some output from the host idk */ \
        0x0A, 0x21, 0x26, \
        HID_REPORT_COUNT   ( 8                                      ) ,\
        HID_OUTPUT         ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
    HID_COLLECTION_END

// clang-format on

uint8_t NintendoSwitchBackend::_descriptor[] = { HID_REPORT_DESC() };

NintendoSwitchBackend::NintendoSwitchBackend(InputSource **input_sources, size_t input_source_count)
    : CommunicationBackend(input_sources, input_source_count) {
    USBDevice.setManufacturerDescriptor("HORI CO.,LTD.");
    USBDevice.setProductDescriptor("POKKEN CONTROLLER");
    USBDevice.setSerialDescriptor("1.0");
    USBDevice.setID(0x0F0D, 0x0092);

    TUCompositeHID::_usb_hid.begin();

    _report = {
        y: false,
        b: false,
        a: false,
        x: false,
        l: false,
        r: false,
        zl: false,
        zr: false,
        minus: false,
        plus: false,
        l3: false,
        r3: false,
        home: false,
        capture: false,
        reserved0: 0,
        hat: SWITCH_HAT_CENTERED,
        lx: 128,
        ly: 128,
        rx: 128,
        ry: 128,
        reserved1: 0,
    };
}

NintendoSwitchBackend::~NintendoSwitchBackend() {}

void NintendoSwitchBackend::RegisterDescriptor() {
    TUCompositeHID::addDescriptor(_descriptor, sizeof(_descriptor));
}

void NintendoSwitchBackend::SendReport() {
    ScanInputs(InputScanSpeed::SLOW);
    ScanInputs(InputScanSpeed::MEDIUM);

    while (!TUCompositeHID::_usb_hid.ready()) {
        tight_loop_contents();
    }

    ScanInputs(InputScanSpeed::FAST);

    UpdateOutputs();

    // Digital outputs
    _report.y = _outputs.y;
    _report.b = _outputs.b;
    _report.a = _outputs.a;
    _report.x = _outputs.x;
    _report.l = _outputs.buttonL;
    _report.r = _outputs.buttonR;
    _report.zl = _outputs.triggerLDigital;
    _report.zr = _outputs.triggerRDigital;
    _report.minus = _outputs.select;
    _report.plus = _outputs.start;
    _report.l3 = _outputs.leftStickClick;
    _report.r3 = _outputs.rightStickClick;
    _report.home = _outputs.home;

    // Analog outputs
    _report.lx = ApplyRadius(ApplyDeadzone(_outputs.leftStickX, 11, true), 256);
    _report.ly = 255 - ApplyRadius(ApplyDeadzone(_outputs.leftStickY, 11, true), 256);
    _report.rx = ApplyRadius(ApplyDeadzone(_outputs.rightStickX, 11, true), 256);
    _report.ry = 255 - ApplyRadius(ApplyDeadzone(_outputs.rightStickY, 11, true), 256);

    // D-pad Hat Switch
    _report.hat =
        GetHatPosition(_outputs.dpadLeft, _outputs.dpadRight, _outputs.dpadDown, _outputs.dpadUp);

    TUCompositeHID::_usb_hid.sendReport(_report_id, &_report, sizeof(switch_gamepad_report_t));
}

uint8_t NintendoSwitchBackend::ApplyDeadzone(uint8_t value, uint8_t deadzone, bool scale) {
    int8_t value_signed = value - 128;
    if (abs(value_signed) > deadzone) {
        // If outside deadzone, must subtract deadzone from result so that axis values start from 1
        // instead of having lower values cut off.
        int8_t post_deadzone = value_signed - deadzone * SIGNUM(value_signed);
        // If a radius value is passed in, scale up the values linearly so that the same effective
        // value is given on the rim.
        if (scale) {
            int8_t post_scaling = min(127, post_deadzone * 128.0 / (128 - deadzone));
            return post_scaling + 128;
        }
        return post_deadzone + 128;
    }
    return 128;
}

uint8_t NintendoSwitchBackend::ApplyRadius(uint8_t value, int radius) {
    int8_t value_signed = value - 128;
    int8_t sign = SIGNUM(value_signed);
    return min(127, abs((int)(value_signed * radius / 128.0))) * sign + 128;
}

switch_gamepad_hat_t NintendoSwitchBackend::GetHatPosition(
    bool left,
    bool right,
    bool down,
    bool up
) {
    switch_gamepad_hat_t angle = SWITCH_HAT_CENTERED;
    if (right && !left) {
        angle = SWITCH_HAT_RIGHT;
        if (down)
            angle = SWITCH_HAT_DOWN_RIGHT;
        if (up)
            angle = SWITCH_HAT_UP_RIGHT;
    } else if (left && !right) {
        angle = SWITCH_HAT_LEFT;
        if (down)
            angle = SWITCH_HAT_DOWN_LEFT;
        if (up)
            angle = SWITCH_HAT_UP_LEFT;
    } else if (down && !up) {
        angle = SWITCH_HAT_DOWN;
    } else if (up && !down) {
        angle = SWITCH_HAT_UP;
    }
    return angle;
}