#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC                      (-1)
#endif

#define IDI_APP_ICON                    101
#define IDR_MAIN_MENU                   102
#define IDR_TRAY_MENU                   103

// Commands
#define ID_APP_EXIT                     1001
#define ID_TASK_NEW                     1002
#define ID_TASK_NEW_VIDEO               1003
#define ID_TASK_START                   1004
#define ID_TASK_PAUSE                   1005
#define ID_TASK_DELETE                  1006
#define ID_TASK_OPEN_FOLDER             1007
#define ID_TASK_OPEN_FILE               1008
#define ID_TASK_SETTINGS                1009
#define ID_EXTENSION_INSTALL            1010
#define ID_ABOUT                        1011
#define ID_TRAY_SHOW                    1012
#define ID_TRAY_EXIT                    1013
#define ID_TASK_COPY_URL                1014
#define ID_TASK_SHOW_INFO               1015

// Dialogs
#define IDD_NEW_DOWNLOAD                2001
#define IDD_NEW_VIDEO                   2002
#define IDD_SETTINGS                    2003
#define IDD_ABOUT                       2004
#define IDD_DOWNLOAD_PROGRESS           2005
#define IDD_DOWNLOAD_INFO               2006

// Dialog Controls - New Download
#define IDC_EDT_URL                     3001
#define IDC_EDT_SAVE_PATH               3002
#define IDC_BTN_BROWSE                  3003
#define IDC_SPN_PARTS                   3004
#define IDC_EDT_PARTS                   3005
#define IDC_CHK_START_IMMEDIATE         3006
#define IDC_LBL_URL                     3007
#define IDC_LBL_SAVETO                  3008
#define IDC_LBL_PARTS                   3009

// Dialog Controls - Video Download
#define IDC_EDT_VIDEO_URL               3101
#define IDC_LBL_VIDEO_STATUS            3102
#define IDC_LST_STREAMS                 3103
#define IDC_BTN_ANALYZE                 3104
#define IDC_EDT_VIDEO_TITLE             3105
#define IDC_EDT_VIDEO_SAVEPATH          3106
#define IDC_BTN_VIDEO_BROWSE            3107

// Dialog Controls - Settings
#define IDC_EDT_DEFAULT_DIR             3201
#define IDC_BTN_DIR_BROWSE              3202
#define IDC_EDT_MAX_TASKS               3203
#define IDC_EDT_DEFAULT_PARTS           3204
#define IDC_CHK_AUTO_START              3205
#define IDC_CHK_STARTUP_MINIMIZED       3206
#define IDC_EDT_PORT                    3207
#define IDC_EDT_INITIAL_PARTS           3208
#define IDC_EDT_MAX_PARTS               3209
#define IDC_CHK_ADAPTIVE_SPLIT          3210
#define IDC_CMB_CATEGORY                3211
#define IDC_EDT_CATEGORY_EXTS           3212
#define IDC_EDT_CATEGORY_DIR            3213
#define IDC_BTN_CATEGORY_BROWSE         3214
#define IDC_BTN_CATEGORY_CLEAR          3215

// Dialog Controls - Download Progress
#define IDC_LBL_PROG_FILENAME           3301
#define IDC_LBL_PROG_URL                3302
#define IDC_LBL_PROG_SAVETO             3303
#define IDC_PRG_PROGRESS                3304
#define IDC_SEGMENT_CONTAINER           3305
#define IDC_LBL_PROG_STATUS             3306
#define IDC_LBL_PROG_SIZE               3307
#define IDC_LBL_PROG_SPEED              3308
#define IDC_LBL_PROG_TIME               3309
#define IDC_LBL_PROG_PARTS              3310
#define IDC_CHK_CLOSE_WHEN_DONE         3311
#define IDC_CHK_OPEN_WHEN_DONE          3311
#define IDC_BTN_PROG_PAUSE              3312
#define IDC_BTN_PROG_CANCEL             3313
#define IDC_BTN_PROG_HIDE               3314
#define IDC_BTN_PROG_OPEN_FILE          3315
#define IDC_BTN_PROG_OPEN_FOLDER        3316
#define IDC_SEGMENT_BAR                 3317

// Dialog Controls - Download Info
#define IDC_LBL_INFO_FILENAME           3401
#define IDC_LBL_INFO_FILESIZE           3402
#define IDC_EDT_INFO_URL                3403
#define IDC_BTN_INFO_COPY_URL           3404
#define IDC_EDT_INFO_SAVEPATH           3405
#define IDC_LBL_INFO_STATUS             3406
#define IDC_LBL_INFO_CREATED            3407
#define IDC_LBL_INFO_COMPLETED          3408
#define IDC_LBL_INFO_PARTS              3409
#define IDC_BTN_INFO_OPEN_FILE          3410
#define IDC_BTN_INFO_OPEN_FOLDER        3411
