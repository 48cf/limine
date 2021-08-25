#include <stdint.h>
#include <stddef.h>
#include <lib/readline.h>
#include <lib/libc.h>
#include <lib/blib.h>
#include <lib/term.h>
#include <lib/print.h>
#if bios == 1
#  include <lib/real.h>
#elif uefi == 1
#  include <efi.h>
#endif

int getchar_internal(uint8_t scancode, uint8_t ascii, uint32_t shift_state) {
    switch (scancode) {
#if bios == 1
        case 0x44:
            return GETCHAR_F10;
        case 0x4b:
            return GETCHAR_CURSOR_LEFT;
        case 0x4d:
            return GETCHAR_CURSOR_RIGHT;
        case 0x48:
            return GETCHAR_CURSOR_UP;
        case 0x50:
            return GETCHAR_CURSOR_DOWN;
        case 0x53:
            return GETCHAR_DELETE;
        case 0x4f:
            return GETCHAR_END;
        case 0x47:
            return GETCHAR_HOME;
        case 0x49:
            return GETCHAR_PGUP;
        case 0x51:
            return GETCHAR_PGDOWN;
        case 0x01:
            return GETCHAR_ESCAPE;
#elif uefi == 1
        case SCAN_F10:
            return GETCHAR_F10;
        case SCAN_LEFT:
            return GETCHAR_CURSOR_LEFT;
        case SCAN_RIGHT:
            return GETCHAR_CURSOR_RIGHT;
        case SCAN_UP:
            return GETCHAR_CURSOR_UP;
        case SCAN_DOWN:
            return GETCHAR_CURSOR_DOWN;
        case SCAN_DELETE:
            return GETCHAR_DELETE;
        case SCAN_END:
            return GETCHAR_END;
        case SCAN_HOME:
            return GETCHAR_HOME;
        case SCAN_PAGE_UP:
            return GETCHAR_PGUP;
        case SCAN_PAGE_DOWN:
            return GETCHAR_PGDOWN;
        case SCAN_ESC:
            return GETCHAR_ESCAPE;
#endif
    }
    switch (ascii) {
        case '\r':
            return '\n';
        case '\b':
            return '\b';
    }

    if (shift_state & (GETCHAR_LCTRL | GETCHAR_RCTRL)) {
        switch (ascii) {
        case 'p': return GETCHAR_CURSOR_UP;
        case 'n': return GETCHAR_CURSOR_DOWN;
        case 'b': return GETCHAR_CURSOR_LEFT;
        case 'f': return GETCHAR_CURSOR_RIGHT;
        default: break;
        }
    }

    // Guard against non-printable values
    if (ascii < 0x20 || ascii > 0x7e) {
        return -1;
    }
    return ascii;
}

#if bios == 1
int getchar(void) {
    uint8_t scancode = 0;
    uint8_t ascii = 0;
    uint32_t mods = 0;
again:;
    struct rm_regs r = {0};
    rm_int(0x16, &r, &r);
    scancode = (r.eax >> 8) & 0xff;
    ascii = r.eax & 0xff;

    r = (struct rm_regs){ 0 };
    r.eax = 0x0200; // GET SHIFT FLAGS
    rm_int(0x16, &r, &r);

    if (r.eax & GETCHAR_LCTRL) {
        /* the bios subtracts 0x60 from ascii if ctrl is pressed */
        mods = GETCHAR_LCTRL;
        ascii += 0x60;
    }

    int ret = getchar_internal(scancode, ascii, mods);
    if (ret == -1)
        goto again;
    return ret;
}

int _pit_sleep_and_quit_on_keypress(uint32_t ticks);

int pit_sleep_and_quit_on_keypress(int seconds) {
    return _pit_sleep_and_quit_on_keypress(seconds * 18);
}
#endif

#if uefi == 1
static EFI_KEY_DATA _read_efi_key(EFI_HANDLE device) {
    EFI_KEY_DATA out = {
        /* default to no modifiers */
        .KeyState = { 0 },
    };

    EFI_GUID exproto_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    EFI_GUID sproto_guid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *exproto = NULL;
    EFI_SIMPLE_TEXT_IN_PROTOCOL *sproto = NULL;

    if (gBS->HandleProtocol(device, &exproto_guid, (void **)&exproto) != EFI_SUCCESS) {
        if (gBS->HandleProtocol(device, &sproto_guid, (void **)&sproto)
            != EFI_SUCCESS) {
            panic("Your input device doesn't have an input protocol!");
        }
    }

    EFI_STATUS status = EFI_UNSUPPORTED;
    if (exproto) {
        status = exproto->ReadKeyStrokeEx(exproto, &out);
    } else {
        status = sproto->ReadKeyStroke(sproto, &out.Key);
    }

    /* there is definitely a key pending here, if there isn't, there's
     * something very wrong, as the caller should be waiting on a key.
     */
    if (status != EFI_SUCCESS) {
        panic("Failed to read from the keyboard");
    }

    if (!(out.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID)) {
        out.KeyState.KeyShiftState = 0;
    }

    return out;
}

int getchar(void) {
again:;
    EFI_KEY_DATA kd;

    UINTN which;

    gBS->WaitForEvent(1, (EFI_EVENT[]){ gST->ConIn->WaitForKey }, &which);

    kd = _read_efi_key(gST->ConsoleInHandle);

    int ret = getchar_internal(kd.Key.ScanCode, kd.Key.UnicodeChar,
                               kd.KeyState.KeyShiftState);

    if (ret == -1) {
        goto again;
    }

    return ret;
}

int pit_sleep_and_quit_on_keypress(int seconds) {
    EFI_KEY_DATA kd;

    UINTN which;

    EFI_EVENT events[2];

    events[0] = gST->ConIn->WaitForKey;

    gBS->CreateEvent(EVT_TIMER, TPL_CALLBACK, NULL, NULL, &events[1]);

    gBS->SetTimer(events[1], TimerRelative, 10000000 * seconds);

again:
    gBS->WaitForEvent(2, events, &which);

    if (which == 1) {
        return 0;
    }

    kd = _read_efi_key(gST->ConsoleInHandle);

    int ret = getchar_internal(kd.Key.ScanCode, kd.Key.UnicodeChar,
                               kd.KeyState.KeyShiftState);

    if (ret == -1) {
        goto again;
    }

    return ret;
}
#endif
