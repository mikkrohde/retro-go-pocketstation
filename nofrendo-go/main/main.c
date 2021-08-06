#include <rg_system.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <nofrendo.h>
#include <nes/nes.h>
#include <nes/input.h>
#include <nes/state.h>

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

static uint16_t myPalette[64];
static rg_video_frame_t frames[2];
static rg_video_frame_t *currentUpdate = &frames[0];
static rg_video_frame_t *previousUpdate = NULL;

static gamepad_state_t joystick1;
static gamepad_state_t *localJoystick = &joystick1;

static bool overscan = true;
static long autocrop = 0;
static bool fullFrame = 0;

static nes_t *nes;

static rg_app_desc_t *app;

#ifdef ENABLE_NETPLAY
static gamepad_state_t *remoteJoystick = &joystick2;
static gamepad_state_t joystick2;
static bool netplay = false;
#endif

static const char *SETTING_AUTOCROP = "autocrop";
static const char *SETTING_OVERSCAN = "overscan";
static const char *SETTING_PALETTE = "palette";
static const char *SETTING_SPRITELIMIT = "spritelimit";
// --- MAIN


static void netplay_handler(netplay_event_t event, void *arg)
{
#ifdef ENABLE_NETPLAY
    bool new_netplay;

    switch (event)
    {
    case NETPLAY_EVENT_STATUS_CHANGED:
        new_netplay = (rg_netplay_status() == NETPLAY_STATUS_CONNECTED);

        if (netplay && !new_netplay)
        {
            rg_gui_alert("Netplay", "Connection lost!");
        }
        else if (!netplay && new_netplay)
        {
            // displayScalingMode = RG_DISPLAY_SCALING_FILL;
            // displayFilterMode = RG_DISPLAY_FILTER_NONE;
            // forceVideoRefresh = true;
            input_connect(INP_JOYPAD1);
            nes_reset(true);
        }

        netplay = new_netplay;
        break;

    default:
        break;
    }

    if (netplay && rg_netplay_mode() == NETPLAY_MODE_GUEST)
    {
        localJoystick = &joystick2;
        remoteJoystick = &joystick1;
    }
    else
    {
        localJoystick = &joystick1;
        remoteJoystick = &joystick2;
    }
#endif
}

static bool screenshot_handler(const char *filename, int width, int height)
{
	return rg_display_save_frame(filename, currentUpdate, width, height);
}

static bool save_state_handler(const char *filename)
{
    return state_save(filename) == 0;
}

static bool load_state_handler(const char *filename)
{
    if (state_load(filename) != 0)
    {
        nes_reset(true);
        return false;
    }
    return true;
}

static bool reset_handler(bool hard)
{
    nes_reset(hard);
    return true;
}


static dialog_return_t sprite_limit_cb(dialog_option_t *option, dialog_event_t event)
{
    bool spritelimit = ppu_getopt(PPU_LIMIT_SPRITES);

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        spritelimit = !spritelimit;
        rg_settings_set_app_int32(SETTING_SPRITELIMIT, spritelimit);
        ppu_setopt(PPU_LIMIT_SPRITES, spritelimit);
    }

    strcpy(option->value, spritelimit ? "On " : "Off");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t overscan_update_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        overscan = !overscan;
        rg_settings_set_app_int32(SETTING_OVERSCAN, overscan);
    }

    strcpy(option->value, overscan ? "Auto" : "Off ");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t autocrop_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int val = autocrop;
    int max = 2;

    if (event == RG_DIALOG_PREV) val = val > 0 ? val - 1 : max;
    if (event == RG_DIALOG_NEXT) val = val < max ? val + 1 : 0;

    if (val != autocrop)
    {
        rg_settings_set_app_int32(SETTING_AUTOCROP, val);
        autocrop = val;
    }

    if (val == 0) strcpy(option->value, "Never ");
    if (val == 1) strcpy(option->value, "Auto  ");
    if (val == 2) strcpy(option->value, "Always");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t advanced_settings_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_ENTER)
    {
        dialog_option_t options[] = {
            {2, "Overscan    ", "Auto ", 1, &overscan_update_cb},
            {3, "Crop sides  ", "Never", 1, &autocrop_update_cb},
            {4, "Sprite limit", "On   ", 1, &sprite_limit_cb},
            RG_DIALOG_CHOICE_LAST
        };
        rg_gui_dialog("Advanced", options, 0);
    }
    return RG_DIALOG_IGNORE;
}

static dialog_return_t palette_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int pal = ppu_getopt(PPU_PALETTE_RGB);
    int max = PPU_PAL_COUNT - 1;

    if (event == RG_DIALOG_PREV) pal = pal > 0 ? pal - 1 : max;
    if (event == RG_DIALOG_NEXT) pal = pal < max ? pal + 1 : 0;

    if (pal != ppu_getopt(PPU_PALETTE_RGB))
    {
        rg_settings_set_app_int32(SETTING_PALETTE, pal);
        ppu_setopt(PPU_PALETTE_RGB, pal);
        rg_display_queue_update(currentUpdate, NULL);
        rg_display_queue_update(currentUpdate, NULL);
    }

    sprintf(option->value, "%.7s", ppu_getpalette(pal)->name);
    return RG_DIALOG_IGNORE;
}


/*
** Video
*/
void osd_blitscreen(uint8_t *bmp)
{
    int crop_v = (overscan) ? nes->overscan : 0;
    int crop_h = (autocrop == 2) || (autocrop == 1 && nes->ppu->left_bg_counter > 210) ? 8 : 0;

    // A rolling average should be used for autocrop == 1, it causes jitter in some games...

    currentUpdate->buffer = NES_SCREEN_GETPTR(bmp, crop_h, crop_v);
    currentUpdate->width = NES_SCREEN_WIDTH - (crop_h * 2);
    currentUpdate->height = NES_SCREEN_HEIGHT - (crop_v * 2);

    // Check if palette needs to be refreshed before we sent to update
    if (nes->ppu->curpal_dirty)
    {
        ppu_rgb_t *pal = nes->ppu->curpal;
        for (int i = 0; i < 64; i++)
        {
            uint16_t c = (pal[i].b >> 3) + ((pal[i].g >> 2) << 5) + ((pal[i].r >> 3) << 11);
            myPalette[i] = (c >> 8) | ((c & 0xff) << 8);
        }
        nes->ppu->curpal_dirty = false;
        previousUpdate = NULL;
    }

    fullFrame = rg_display_queue_update(currentUpdate, previousUpdate) == RG_UPDATE_FULL;

    previousUpdate = currentUpdate;
    currentUpdate = &frames[currentUpdate == &frames[0]];
}

void osd_getinput(void)
{
    unsigned input = 0;

    *localJoystick = rg_input_read_gamepad();

    if (*localJoystick & GAMEPAD_KEY_MENU)
    {
        rg_gui_game_menu();
    }
    else if (*localJoystick & GAMEPAD_KEY_VOLUME)
    {
        dialog_option_t options[] = {
            {100, "Palette", "Default", 1, &palette_update_cb},
            {101, "More...", NULL, 1, &advanced_settings_cb},
            RG_DIALOG_CHOICE_LAST};
        rg_gui_game_settings_menu(options);
    }

#ifdef ENABLE_NETPLAY
    if (netplay)
    {
        rg_netplay_sync(localJoystick, remoteJoystick, sizeof(gamepad_state_t));
        if (joystick2 & GAMEPAD_KEY_START)  input |= INP_PAD_START;
        if (joystick2 & GAMEPAD_KEY_SELECT) input |= INP_PAD_SELECT;
        if (joystick2 & GAMEPAD_KEY_UP)     input |= INP_PAD_UP;
        if (joystick2 & GAMEPAD_KEY_RIGHT)  input |= INP_PAD_RIGHT;
        if (joystick2 & GAMEPAD_KEY_DOWN)   input |= INP_PAD_DOWN;
        if (joystick2 & GAMEPAD_KEY_LEFT)   input |= INP_PAD_LEFT;
        if (joystick2 & GAMEPAD_KEY_A)      input |= INP_PAD_A;
        if (joystick2 & GAMEPAD_KEY_B)      input |= INP_PAD_B;
        input_update(INP_JOYPAD1, input);
        input = 0;
    }
#endif

    if (joystick1 & GAMEPAD_KEY_START)  input |= INP_PAD_START;
    if (joystick1 & GAMEPAD_KEY_SELECT) input |= INP_PAD_SELECT;
    if (joystick1 & GAMEPAD_KEY_UP)     input |= INP_PAD_UP;
    if (joystick1 & GAMEPAD_KEY_RIGHT)  input |= INP_PAD_RIGHT;
    if (joystick1 & GAMEPAD_KEY_DOWN)   input |= INP_PAD_DOWN;
    if (joystick1 & GAMEPAD_KEY_LEFT)   input |= INP_PAD_LEFT;
    if (joystick1 & GAMEPAD_KEY_A)      input |= INP_PAD_A;
    if (joystick1 & GAMEPAD_KEY_B)      input |= INP_PAD_B;

    input_update(INP_JOYPAD0, input);
}


void app_main(void)
{
    rg_emu_proc_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .netplay = &netplay_handler,
        .screenshot = &screenshot_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers);

    frames[0].flags = RG_PIXEL_PAL|RG_PIXEL_565|RG_PIXEL_BE;
    frames[0].stride = NES_SCREEN_PITCH;
    frames[0].pixel_mask = 0x3F;
    frames[0].palette = myPalette;
    frames[1] = frames[0];

    overscan = rg_settings_get_app_int32(SETTING_OVERSCAN, 1);
    autocrop = rg_settings_get_app_int32(SETTING_AUTOCROP, 0);

    nes = nes_init(SYS_DETECT, AUDIO_SAMPLE_RATE, true);
    if (!nes)
    {
        RG_PANIC("Init failed.");
    }

    if (!nes_insertcart(app->romPath))
    {
        RG_PANIC("Unsupported ROM.");
    }

    ppu_setopt(PPU_LIMIT_SPRITES, rg_settings_get_app_int32(SETTING_SPRITELIMIT, 1));
    ppu_setopt(PPU_PALETTE_RGB, rg_settings_get_app_int32(SETTING_PALETTE, 0));
    app->refreshRate = nes->refresh_rate;

    nes_emulate(false);
    nes_emulate(false);

    if (app->startAction == RG_START_ACTION_RESUME)
    {
        rg_emu_load_state(0);
    }

    int frameTime = get_frame_time(nes->refresh_rate);
    int skipFrames = 0;
    int elapsed = 0;

    while (true)
    {
        int64_t startTime = get_elapsed_time();
        bool drawFrame = (skipFrames == 0);

        nes_emulate(drawFrame);

        elapsed = get_elapsed_time_since(startTime);

        if (skipFrames == 0)
        {
            if (app->speedupEnabled)
                skipFrames = app->speedupEnabled * 2;
            else if (elapsed >= frameTime) // Frame took too long
                skipFrames = 1;
            else if (drawFrame && fullFrame) // This could be avoided when scaling != full
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }

        // Tick before submitting audio/syncing
        rg_system_tick(elapsed);

        // Use audio to throttle emulation
        if (!app->speedupEnabled)
        {
            rg_audio_submit(nes->apu->buffer, nes->apu->samples_per_frame);
        }
    }

    RG_PANIC("Nofrendo died!");
}
