#include <sstream>
#include <iostream>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <thread>
#include <iomanip>

#include <orbis/libkernel.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UsbStorage.h>

#define MDIALOG_OK       0
#define MDIALOG_YESNO    1

#include "common/log.h"
#include "common/graphics.cpp"
#include "controller.h"
#include "merger_core.hpp"

std::stringstream debugLogStream;

#define FRAME_WIDTH     1920
#define FRAME_HEIGHT    1080
#define FRAME_DEPTH        4

#define FONT_SIZE   	   32

Color bgColor;
Color fgColor;
FT_Face fontTxt;
int frameID = 0;

std::stringstream userTextStream;

// =================================================================================================

static inline void _orbisCommonDialogSetMagicNumber(uint32_t* magic, const OrbisCommonDialogBaseParam* param)
{
    *magic = (uint32_t)(ORBIS_COMMON_DIALOG_MAGIC_NUMBER + (uint64_t)param);
}

static inline void _orbisCommonDialogBaseParamInit(OrbisCommonDialogBaseParam *param)
{
    memset(param, 0x0, sizeof(OrbisCommonDialogBaseParam));
    param->size = (uint32_t)sizeof(OrbisCommonDialogBaseParam);
    _orbisCommonDialogSetMagicNumber(&(param->magic), param);
}

static inline void orbisMsgDialogParamInitialize(OrbisMsgDialogParam *param)
{
    memset(param, 0x0, sizeof(OrbisMsgDialogParam));
    _orbisCommonDialogBaseParamInit(&param->baseParam);
    param->size = sizeof(OrbisMsgDialogParam);
}

int show_dialog(int dialog_type, const char * format, ...)
{
    OrbisMsgDialogParam param;
    OrbisMsgDialogUserMessageParam userMsgParam;
    OrbisMsgDialogResult result;

    char str[0x800];
    memset(str, 0, sizeof(str));

    va_list opt;
    va_start(opt, format);
    vsprintf(str, format, opt);
    va_end(opt);

    sceMsgDialogInitialize();
    orbisMsgDialogParamInitialize(&param);
    param.mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;

    memset(&userMsgParam, 0, sizeof(userMsgParam));
    userMsgParam.msg = str;
    userMsgParam.buttonType = (dialog_type ? ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO : ORBIS_MSG_DIALOG_BUTTON_TYPE_OK);
    param.userMsgParam = &userMsgParam;

    if (sceMsgDialogOpen(&param) < 0)
        return 0;

    do { } while (sceMsgDialogUpdateStatus() != ORBIS_COMMON_DIALOG_STATUS_FINISHED);
    sceMsgDialogClose();

    memset(&result, 0, sizeof(result));
    sceMsgDialogGetResult(&result);
    sceMsgDialogTerminate();

    return (result.buttonId == ORBIS_MSG_DIALOG_BUTTON_ID_YES);
}

std::vector<std::string> list_files(const char* usb_path)
{
    std::vector<std::string> file_list;
    DIR* dir;
    struct dirent* entry;
    struct stat file_stat;

    dir = opendir(usb_path);
    if (dir == NULL)
    {
        userTextStream << "Failed to open " << usb_path << " directory\n";
        return file_list;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", usb_path, entry->d_name);

        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode))
        {
            file_list.push_back(entry->d_name);
        }
    }

    closedir(dir);
    return file_list;
}

bool compareFilesBySuffix(const std::string& a, const std::string& b)
{
    std::string suffixA = a.substr(a.size() - 12);
    std::string suffixB = b.substr(b.size() - 12);

    if (suffixA.size() != suffixB.size()) return suffixA.size() < suffixB.size();
    return suffixA < suffixB;
}

std::uint64_t get_file_size(const std::string& file_path)
{
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(file_stat.st_size);
}

std::string get_base_filename(const std::string& file_name)
{
    size_t pos = file_name.find_last_of('_');
    if (pos == std::string::npos)
    {
        pos = file_name.find_last_of('.');
    }
    if (pos != std::string::npos)
    {
        return file_name.substr(0, pos);
    }
    return file_name;
}

std::string formatTime(std::uint64_t seconds)
{
    std::uint64_t hours = seconds / 3600;
    std::uint64_t minutes = (seconds % 3600) / 60;
    std::uint64_t secs = seconds % 60;

    std::ostringstream oss;

    if (hours > 0)
    {
        oss << hours << ":";
    }

    oss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << secs;

    return oss.str();
}

void merge_files(const std::vector<std::string>& files, const std::string& output_path)
{
    userTextStream << "\nMerging in progress... Please wait.\n";
    merger::MergeResult res = merger::perform_merge("/data/pkg_merger", files, output_path);

    if (res.status == merger::MergeStatus::SUCCESS)
    {
        userTextStream << "\n[SUCCESS] Files successfully merged into " << output_path << "\n"
                       << "Total bytes written: " << res.bytes_written << "\n"
                       << "Now you can install it via goldhen's installer!\n"
                       << "Now you can also delete .pkgpart files from /data/pkg_merger\n"
                       << "Close and reopen this app to start again\n";
    }
    else
    {
        userTextStream << "\n[FAILED] Merge failed: " << res.error_message << "\n"
                       << "Any incomplete output file has been removed to prevent corruption.\n"
                       << "Please verify disk space and input integrity before retrying.\n";
    }
}


int main(void)
{   
    setvbuf(stdout, NULL, _IONBF, 0);

    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG) < 0 ||
        sceCommonDialogInitialize() < 0)
    {
        printf("Failed to initialize CommonDialog\n");
        for(;;);
    }

    int rc;
    int video;
    int curFrame = 0;
    
    DEBUGLOG << "Creating a scene";
    
    auto scene = new Scene2D(FRAME_WIDTH, FRAME_HEIGHT, FRAME_DEPTH);
    
    if(!scene->Init(0xC000000, 2))
    {
        DEBUGLOG << "Failed to initialize 2D scene";
        for(;;);
    }

    bgColor = { 61, 116, 231 };
    fgColor = { 255, 255, 255 };

    const char *font = "/app0/assets/fonts/Montserrat-Regular.ttf";
    
    DEBUGLOG << "Initializing font (" << font << ")";

    if(!scene->InitFont(&fontTxt, font, FONT_SIZE))
    {
        DEBUGLOG << "Failed to initialize font '" << font << "'";
        for(;;);
    }

    userTextStream << "Welcome! PKG merger (hardened)\nSearching in /data/pkg_merger...\n";
    std::vector<std::string> raw_files = list_files("/data/pkg_merger");
    merger::ValidationResult validation = merger::validate_and_prepare_parts(raw_files);

    std::vector<std::string> files = validation.sorted_files;
    auto controller = new Controller();
    bool listen = false;

    if (validation.status != merger::ValidationStatus::OK)
    {
        userTextStream << "\n[ERROR] " << validation.error_message << "\n";
        if (validation.status == merger::ValidationStatus::EMPTY_INPUT)
        {
            userTextStream << "You must split your pkgs on PC and move them from /mnt/usb0 to /data/pkg_merger using FTP\n";
        }
    }
    else
    {
        userTextStream << "Found " << files.size() << " valid parts for '" << validation.single_base_name << "':\n";
        for (const auto &file : files)
        {
            userTextStream << " - " << file << "\n";
        }

        std::uint64_t totalSize = merger::calculate_total_parts_size("/data/pkg_merger", files);
        const std::uint64_t speed = 27 * 1024 * 1024; // 27 MB/s on average
        std::uint64_t estimatedTimeInSeconds = (speed > 0) ? (totalSize / speed) : 0;

        userTextStream << "\nTotal size: " << (totalSize / (1024 * 1024)) << " MB\n";
        userTextStream << "Estimated time: " << formatTime(estimatedTimeInSeconds) << "\n";

        std::string baseFileName = validation.single_base_name;
        std::string outputPath = "/data/pkg/" + baseFileName + ".pkg";

        // [P1] Clean any stale temporary merge file (<output>.tmp.merging) from prior interrupted runs
        // before checking free space so it doesn't block its own retry (Fix #7 / Review P1)
        merger::clean_stale_temp_file(outputPath);

        // Pre-flight free-space check with 2x multiplier (Fix #7, Review P2)
        uint64_t required_space = 0;
        bool mult_ok = merger::compute_required_space(totalSize, merger::FREE_SPACE_MULTIPLIER, required_space);

        uint64_t available_space = 0;
        if (!mult_ok)
        {
            userTextStream << "\n[ERROR] Package total size exceeds maximum supported limits.\n";
        }
        else if (merger::get_available_space("/data/pkg", available_space))
        {
            userTextStream << "Available space on /data/pkg: " << (available_space / (1024 * 1024)) << " MB\n";
            userTextStream << "Required free space (" << merger::FREE_SPACE_MULTIPLIER << "x): " << (required_space / (1024 * 1024)) << " MB\n";

            if (available_space < required_space)
            {
                userTextStream << "\n[ERROR] Insufficient disk space!\n"
                               << "Required (2x package size): " << (required_space / (1024 * 1024)) << " MB\n"
                               << "Available: " << (available_space / (1024 * 1024)) << " MB.\n"
                               << "Please free up disk space on PS4 internal storage before merging.\n";
            }
            else
            {
                userTextStream << "App will be frozen entire time, do not worry and look\nif .pkg file started appearing in /data/pkg directory via FTP\nAllow up to 3x of that estimated time\n";
                userTextStream << "\nPress any button on controller to START merging parts\n\n";
                if (!controller->Init(-1))
                {
                    userTextStream << "Couldn't initialize controller\n";
                    for (;;);
                }
                listen = true;
            }
        }

        else
        {
            userTextStream << "\n[WARNING] Could not check free space on /data/pkg. Proceed with caution.\n";
            userTextStream << "\nPress any button on controller to START merging parts\n\n";
            if (!controller->Init(-1))
            {
                userTextStream << "Couldn't initialize controller\n";
                for (;;);
            }
            listen = true;
        }
    }

    
    for (;;)
    {
        scene->DrawText((char *)userTextStream.str().c_str(), fontTxt, 150, 150, bgColor, fgColor);

        // Submit the frame buffer
        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);

        // Swap to the next buffer
        scene->FrameBufferSwap();
        frameID++;

        if (listen)
        {
            if (controller->TrianglePressed() 
                || controller->CirclePressed() 
                || controller->XPressed() 
                || controller->SquarePressed()
                || controller->L1Pressed()
                || controller->L2Pressed()
                || controller->R1Pressed()
                || controller->R2Pressed()
                || controller->L3Pressed()
                || controller->R3Pressed()
                || controller->StartPressed()
                || controller->DpadUpPressed()
                || controller->DpadRightPressed()
                || controller->DpadDownPressed()
                || controller->DpadLeftPressed()
                || controller->TouchpadPressed()
            )
            {
                listen = false;
                std::string baseFileName = validation.single_base_name;
                std::string outputPath = "/data/pkg/" + baseFileName + ".pkg";

                // Overwrite confirmation check (Fix #5)
                if (merger::file_exists(outputPath))
                {
                    if (!show_dialog(MDIALOG_YESNO, "Output file %s already exists!\nDo you want to overwrite it?", outputPath.c_str()))
                    {
                        userTextStream << "\nMerge cancelled by user (overwrite declined).\n";
                        listen = true;
                        continue;
                    }
                }

                if (show_dialog(MDIALOG_OK, "App will not report any progress and will be frozen until merging is done, do not worry about it. Press OK to start merging or exit app now"))
                {
                    merge_files(files, outputPath);
                } else {
                    listen = true;
                }
            }
        }
    }

    return 0;
}
