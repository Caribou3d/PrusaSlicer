///|/ Copyright (c) Prusa Research 2021 - 2023 David Kocík @kocikdav, Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas
///|/ Copyright (c) 2022 KARBOWSKI Piotr
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifdef __linux__
#include "DesktopIntegrationDialog.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Utils/DirectoriesUtils.hpp"

#include <boost/nowide/fstream.hpp> // IWYU pragma: keep
#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <wx/filename.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

namespace {

// escaping of path string according to
// https://cgit.freedesktop.org/xdg/xdg-specs/tree/desktop-entry/desktop-entry-spec.xml
std::string escape_string(const std::string& str)
{
    // The buffer needs to be bigger if escaping <,>,&
    std::vector<char> out(str.size() * 4, 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        // must be escaped
        if (c == '\"') { //double quote
            (*outptr ++) = '\\';
            (*outptr ++) = '\"';
        } else if (c == '`') {  // backtick character
            (*outptr ++) = '\\';
            (*outptr ++) = '`';
        } else if (c == '$') { // dollar sign
            (*outptr ++) = '\\';
            (*outptr ++) = '$';
        } else if (c == '\\') { // backslash character
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
        //  Reserved characters
        // At Ubuntu, all these characters must NOT be escaped for desktop integration to work
        /*
        } else if (c == ' ') { // space
            (*outptr ++) = '\\';
            (*outptr ++) = ' ';
        } else if (c == '\t') { // tab
            (*outptr ++) = '\\';
            (*outptr ++) = '\t';
        } else if (c == '\n') { // newline
            (*outptr ++) = '\\';
            (*outptr ++) = '\n';
        } else if (c == '\'') { // single quote
            (*outptr ++) = '\\';
            (*outptr ++) = '\'';
        } else if (c == '>') { // greater-than sign
            (*outptr ++) = '\\';
            (*outptr ++) = '&';
            (*outptr ++) = 'g';
            (*outptr ++) = 't';
            (*outptr ++) = ';';
        } else if (c == '<') { //less-than sign
            (*outptr ++) = '\\';
            (*outptr ++) = '&';
            (*outptr ++) = 'l';
            (*outptr ++) = 't';
            (*outptr ++) = ';';
        }  else if (c == '~') { // tilde
            (*outptr ++) = '\\';
            (*outptr ++) = '~';
        } else if (c == '|') { // vertical bar
            (*outptr ++) = '\\';
            (*outptr ++) = '|';
        } else if (c == '&') { // ampersand
            (*outptr ++) = '\\';
            (*outptr ++) = '&';
            (*outptr ++) = 'a';
            (*outptr ++) = 'm';
            (*outptr ++) = 'p';
            (*outptr ++) = ';';
        } else if (c == ';') { // semicolon
            (*outptr ++) = '\\';
            (*outptr ++) = ';';
        } else if (c == '*') { //asterisk
            (*outptr ++) = '\\';
            (*outptr ++) = '*';
        } else if (c == '?') { // question mark
            (*outptr ++) = '\\';
            (*outptr ++) = '?';
        } else if (c == '#') { // hash mark
            (*outptr ++) = '\\';
            (*outptr ++) = '#';
        } else if (c == '(') { // parenthesis
            (*outptr ++) = '\\';
            (*outptr ++) = '(';
        } else if (c == ')') {
            (*outptr ++) = '\\';
            (*outptr ++) = ')';
        */
        } else
            (*outptr ++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}
// Disects path strings stored in system variable divided by ':' and adds into vector
void resolve_path_from_var(const std::string& var, std::vector<std::string>& paths)
{
    wxString wxdirs;
    if (! wxGetEnv(boost::nowide::widen(var), &wxdirs) || wxdirs.empty() )
        return;
    std::string dirs = into_u8(wxdirs);
    for (size_t i = dirs.find(':'); i != std::string::npos; i = dirs.find(':'))
    {
        paths.push_back(dirs.substr(0, i));
        if (dirs.size() > i+1)
            dirs = dirs.substr(i+1);
    }
    paths.push_back(dirs);
}
// Return true if directory in path p+dir_name exists
bool contains_path_dir(const std::string& p, const std::string& dir_name)
{
    if (p.empty() || dir_name.empty())
       return false;
    boost::filesystem::path path(p + (p[p.size()-1] == '/' ? "" : "/") + dir_name);
    boost::system::error_code ec;
    if (boost::filesystem::exists(path, ec) && !ec) {
        //BOOST_LOG_TRIVIAL(debug) << path.string() << " " << std::oct << boost::filesystem::status(path).permissions();
        return boost::filesystem::is_directory(path); //boost::filesystem::status(path).permissions() & boost::filesystem::owner_write;
    } else
        BOOST_LOG_TRIVIAL(debug) << path.string() << " doesnt exists";
    return false;
}

boost::filesystem::path get_existing_dir(const std::string& sub, const std::string& dir_name)
{
    assert(!sub.empty() && !dir_name.empty());
    boost::filesystem::path path = boost::filesystem::path(sub) / dir_name;
    boost::system::error_code ec;
    if (!boost::filesystem::exists(path, ec) || ec) {
        return boost::filesystem::path();
    }
    if (!boost::filesystem::is_directory(path, ec) || ec) {
        return boost::filesystem::path();
    }
    return path;
}
// Creates directory in path if not exists yet
void create_dir(const boost::filesystem::path& path)
{
    boost::system::error_code ec;
    if (boost::filesystem::exists(path, ec) && !ec)
        return;
    BOOST_LOG_TRIVIAL(debug)<< "creating " << path.string();
    boost::filesystem::create_directory(path, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error)<< "create directory failed: " << ec.message();
}
// Starts at basic_path (excluded) and creates all directories in dir_path
void create_path(const std::string& basic_path, const std::string& dir_path)
{
    if (basic_path.empty() || dir_path.empty())
       return;

    boost::filesystem::path path(basic_path);
    std::string dirs = dir_path;
    for (size_t i = dirs.find('/'); i != std::string::npos; i = dirs.find('/'))
    {
        std::string dir = dirs.substr(0, i);
        path = boost::filesystem::path(path.string() +"/"+ dir);
        create_dir(path);
        dirs = dirs.substr(i+1);
    }
    path = boost::filesystem::path(path.string() +"/"+ dirs);
    create_dir(path);
}
// Calls our internal copy_file function to copy file at icon_path to dest_path
bool copy_icon(const std::string& icon_path, const std::string& dest_path)
{
    BOOST_LOG_TRIVIAL(debug) <<"icon from "<< icon_path;
    BOOST_LOG_TRIVIAL(debug) <<"icon to "<< dest_path;
    std::string error_message;
    auto cfr = copy_file(icon_path, dest_path, error_message, false);
    if (cfr) {
        BOOST_LOG_TRIVIAL(debug) << "Copy icon fail(" << cfr << "): " << error_message;
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Copy icon success.";
    return true;
}
// Creates new file filled with data.
bool create_desktop_file(const std::string& path, const std::string& data)
{
    BOOST_LOG_TRIVIAL(debug) <<".desktop to "<< path;
    boost::nowide::ofstream output(path);
    output << data;
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0)
    {
        BOOST_LOG_TRIVIAL(debug) << "Desktop file created.";
        return true;
    }
    BOOST_LOG_TRIVIAL(debug) << "Desktop file NOT created.";
    return false;
}
} // namespace integratec_desktop_internal

// methods that actually do / undo desktop integration. Static to be accesible from anywhere.
bool DesktopIntegrationDialog::is_integrated()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path(app_config->get("desktop_integration_app_path"));
    BOOST_LOG_TRIVIAL(debug) << "Desktop integration desktop file path: " << path;

    if (path.empty())
        return false;

    // confirmation that PrusaSlicer.desktop exists
    struct stat buffer;
    return (stat (path.c_str(), &buffer) == 0);
}
bool DesktopIntegrationDialog::integration_possible()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    return true;
}
void DesktopIntegrationDialog::perform_desktop_integration()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
	BOOST_LOG_TRIVIAL(debug) << "performing desktop integration.";
    // Path to appimage
    const char *appimage_env = std::getenv("APPIMAGE");
    std::string excutable_path;
    if (appimage_env) {
        try {
            excutable_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env)).string();
        } catch (std::exception &) {
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - boost::filesystem::canonical did not return appimage path.";
            show_error(nullptr, _L("Performing desktop integration failed - boost::filesystem::canonical did not return appimage path."));
            return;
        }
    } else {
        // not appimage - find executable
        excutable_path = boost::dll::program_location().string();
        //excutable_path = wxStandardPaths::Get().GetExecutablePath().string();
        BOOST_LOG_TRIVIAL(debug) << "non-appimage path to executable: " << excutable_path;
        if (excutable_path.empty())
        {
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - no executable found.";
            show_error(nullptr, _L("Performing desktop integration failed - Could not find executable."));
            return;
        }
    }

    // Escape ' characters in appimage, other special symbols will be esacaped in desktop file by 'excutable_path'
    //boost::replace_all(excutable_path, "'", "'\\''");
    excutable_path = escape_string(excutable_path);

    // Find directories icons and applications
    // $XDG_DATA_HOME defines the base directory relative to which user specific data files should be stored.
    // If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used.
    // $XDG_DATA_DIRS defines the preference-ordered set of base directories to search for data files in addition to the $XDG_DATA_HOME base directory.
    // The directories in $XDG_DATA_DIRS should be seperated with a colon ':'.
    // If $XDG_DATA_DIRS is either not set or empty, a value equal to /usr/local/share/:/usr/share/ should be used.
    std::vector<std::string>target_candidates;
    resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    resolve_path_from_var("XDG_DATA_DIRS", target_candidates);

    AppConfig *app_config = wxGetApp().app_config;
    // suffix string to create different desktop file for alpha, beta.

    std::string version_suffix;
    std::string name_suffix;
    std::string version(SLIC3R_VERSION);
    if (version.find("alpha") != std::string::npos)
    {
        version_suffix = "-alpha";
        name_suffix = " - alpha";
    }else if (version.find("beta") != std::string::npos)
    {
        version_suffix = "-beta";
        name_suffix = " - beta";
    }

    // theme path to icon destination
    std::string icon_theme_path;
    std::string icon_theme_dirs;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        icon_theme_path = "hicolor/96x96/apps/";
        icon_theme_dirs = "/hicolor/96x96/apps";
    }

    std::string target_dir_icons;
    std::string target_dir_desktop;

    // slicer icon
    // iterate thru target_candidates to find icons folder
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        // Copy icon PrusaSlicer.png from resources_dir()/icons to target_dir_icons/icons/
        if (contains_path_dir(target_candidates[i], "icons")) {
            target_dir_icons = target_candidates[i];
            std::string icon_path = GUI::format("%1%/icons/PrusaSlicer.png",resources_dir());
            std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
            if (copy_icon(icon_path, dest_path))
                break; // success
            else
                target_dir_icons.clear(); // copying failed
        }
        // if all failed - try creating default home folder
        if (i == target_candidates.size() - 1) {
            // create $HOME/.local/share
              create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/icons" + icon_theme_dirs);
              // copy icon
             target_dir_icons = GUI::format("%1%/.local/share",wxFileName::GetHomeDir());
              std::string icon_path = GUI::format("%1%/icons/PrusaSlicer.png",resources_dir());
              std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
             if (!contains_path_dir(target_dir_icons, "icons")
                || !copy_icon(icon_path, dest_path)) {
                // every attempt failed - icon wont be present
                target_dir_icons.clear();
             }
        }
    }
    if(target_dir_icons.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Copying PrusaSlicer icon to icons directory failed.";
    } else
    	// save path to icon
        app_config->set("desktop_integration_icon_slicer_path", GUI::format("%1%/icons/%2%PrusaSlicer%3%.png", target_dir_icons, icon_theme_path, version_suffix));

    // desktop file
    // iterate thru target_candidates to find applications folder

    std::string desktop_file = GUI::format(
        "[Desktop Entry]\n"
        "Name=PrusaSlicer%1%\n"
        "GenericName=3D Printing Software\n"
        "Icon=PrusaSlicer%2%\n"
        "Exec=\"%3%\" %%F\n"
        "Terminal=false\n"
        "Type=Application\n"
        "MimeType=model/stl;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;\n"
        "Categories=Graphics;3DGraphics;Engineering;\n"
        "Keywords=3D;Printing;Slicer;slice;3D;printer;convert;gcode;stl;obj;amf;SLA\n"
        "StartupNotify=false\n"
        "StartupWMClass=prusa-slicer\n", name_suffix, version_suffix, excutable_path);

    bool candidate_found = false;
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        if (contains_path_dir(target_candidates[i], "applications")) {
            target_dir_desktop = target_candidates[i];
            // Write slicer desktop file
            std::string path = GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix);
            if (create_desktop_file(path, desktop_file)) {
                candidate_found = true;
                BOOST_LOG_TRIVIAL(debug) << "PrusaSlicer.desktop file installation success.";
                break;
            }
            else {
                // write failed - try another path
                BOOST_LOG_TRIVIAL(debug) << "Attempt to PrusaSlicer.desktop file installation failed. failed path: " << target_candidates[i];
                target_dir_desktop.clear();
            }
        }
    }
    // if all failed - try creating default home folder
    if (!candidate_found) {
        // create $HOME/.local/share
        create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/applications");
        // create desktop file
        target_dir_desktop = GUI::format("%1%/.local/share", wxFileName::GetHomeDir());
        std::string path = GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix);
        if (contains_path_dir(target_dir_desktop, "applications")) {
            if (!create_desktop_file(path, desktop_file)) {
                // Desktop file not written - end desktop integration
                BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not create desktop file";
                return;
            }
        }
        else {
            // Desktop file not written - end desktop integration
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed because the application directory was not found.";
            return;
        }
    }
    assert(!target_dir_desktop.empty());
    if (target_dir_desktop.empty()) {
        // Desktop file not written - end desktop integration
        BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed because the application directory was not found.";
        show_error(nullptr, _L("Performing desktop integration failed because the application directory was not found."));
        return;
    }
    // save path to desktop file
    app_config->set("desktop_integration_app_path", GUI::format("%1%/applications/PrusaSlicer%2%.desktop", target_dir_desktop, version_suffix));

    // Repeat for Gcode viewer - use same paths as for slicer files
    // Do NOT add gcode viewer desktop file on ChromeOS
    if (platform_flavor() != PlatformFlavor::LinuxOnChromium) {
        // Icon
        if (!target_dir_icons.empty())
        {
            std::string icon_path = GUI::format("%1%/icons/PrusaSlicer-gcodeviewer_192px.png",resources_dir());
            std::string dest_path = GUI::format("%1%/icons/%2%PrusaSlicer-gcodeviewer%3%.png", target_dir_icons, icon_theme_path, version_suffix);
            if (copy_icon(icon_path, dest_path))
                // save path to icon
                app_config->set("desktop_integration_icon_viewer_path", dest_path);
            else
                BOOST_LOG_TRIVIAL(error) << "Copying Gcode Viewer icon to icons directory failed.";
        }

        // Desktop file
        std::string desktop_file_viewer = GUI::format(
            "[Desktop Entry]\n"
            "Name=Prusa Gcode Viewer%1%\n"
            "GenericName=3D Printing Software\n"
            "Icon=PrusaSlicer-gcodeviewer%2%\n"
            "Exec=\"%3%\" --gcodeviewer %%F\n"
            "Terminal=false\n"
            "Type=Application\n"
            "MimeType=text/x.gcode;\n"
            "Categories=Graphics;3DGraphics;\n"
            "Keywords=3D;Printing;Slicer;\n"
            "StartupNotify=false\n", name_suffix, version_suffix, excutable_path);
        std::string desktop_path = GUI::format("%1%/applications/PrusaSlicerGcodeViewer%2%.desktop", target_dir_desktop, version_suffix);
        if (create_desktop_file(desktop_path, desktop_file_viewer))
            // save path to desktop file
            app_config->set("desktop_integration_app_viewer_path", desktop_path);
        else {
            BOOST_LOG_TRIVIAL(error) << "Performing desktop integration failed - could not create Gcodeviewer desktop file";
            show_error(nullptr, _L("Performing desktop integration failed - could not create Gcodeviewer desktop file. PrusaSlicer desktop file was probably created successfully."));
        }
    }
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::undo_desktop_integration()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    const AppConfig *app_config = wxGetApp().app_config;
    // slicer .desktop
    std::string path = std::string(app_config->get("desktop_integration_app_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    // slicer icon
    path = std::string(app_config->get("desktop_integration_icon_slicer_path"));
    if (!path.empty()) {
    	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    // No gcode viewer at ChromeOS
    if (platform_flavor() != PlatformFlavor::LinuxOnChromium) {
        // gcode viewer .desktop
        path = std::string(app_config->get("desktop_integration_app_viewer_path"));
        if (!path.empty()) {
        	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
            std::remove(path.c_str());
        }
         // gcode viewer icon
        path = std::string(app_config->get("desktop_integration_icon_viewer_path"));
        if (!path.empty()) {
        	BOOST_LOG_TRIVIAL(debug) << "removing " << path;
            std::remove(path.c_str());
        }
    }
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::UndoDesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::perform_downloader_desktop_integration()
{
    BOOST_LOG_TRIVIAL(debug) << "performing downloader desktop integration.";
    // Path to appimage
    const char* appimage_env = std::getenv("APPIMAGE");
    std::string excutable_path;
    if (appimage_env) {
        try {
            excutable_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env)).string();
        }
        catch (std::exception&) {
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - boost::filesystem::canonical did not return appimage path.";
            show_error(nullptr, _L("Performing downloader desktop integration failed - boost::filesystem::canonical did not return appimage path."));
            return;
        }
    }
    else {
        // not appimage - find executable
        excutable_path = boost::dll::program_location().string();
        //excutable_path = wxStandardPaths::Get().GetExecutablePath().string();
        BOOST_LOG_TRIVIAL(debug) << "non-appimage path to executable: " << excutable_path;
        if (excutable_path.empty())
        {
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - no executable found.";
            show_error(nullptr, _L("Performing downloader desktop integration failed - Could not find executable."));
            return;
        }
    }

    // Escape ' characters in appimage, other special symbols will be esacaped in desktop file by 'excutable_path'
    //boost::replace_all(excutable_path, "'", "'\\''");
    excutable_path = escape_string(excutable_path);

    // Find directories icons and applications
    // $XDG_DATA_HOME defines the base directory relative to which user specific data files should be stored.
    // If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used.
    // $XDG_DATA_DIRS defines the preference-ordered set of base directories to search for data files in addition to the $XDG_DATA_HOME base directory.
    // The directories in $XDG_DATA_DIRS should be seperated with a colon ':'.
    // If $XDG_DATA_DIRS is either not set or empty, a value equal to /usr/local/share/:/usr/share/ should be used.
    std::vector<std::string>target_candidates;
    resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    resolve_path_from_var("XDG_DATA_DIRS", target_candidates);

    AppConfig* app_config = wxGetApp().app_config;
    // suffix string to create different desktop file for alpha, beta.

    std::string version_suffix;
    std::string name_suffix;
    std::string version(SLIC3R_VERSION);
    if (version.find("alpha") != std::string::npos)
    {
        version_suffix = "_alpha";
        name_suffix = " - alpha";
    }
    else if (version.find("beta") != std::string::npos)
    {
        version_suffix = "_beta";
        name_suffix = " - beta";
    }

    // theme path to icon destination
    std::string icon_theme_path;
    std::string icon_theme_dirs;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        icon_theme_path = "hicolor/96x96/apps/";
        icon_theme_dirs = "/hicolor/96x96/apps";
    }

    std::string target_dir_desktop;

    // desktop file
    // iterate thru target_candidates to find applications folder

    std::string desktop_file_downloader = GUI::format(
        "[Desktop Entry]\n"
        "Name=PrusaSlicer URL Protocol%1%\n"
        "Exec=\"%2%\" --single-instance %%u\n"
        "Terminal=false\n"
        "Type=Application\n"
        "MimeType=x-scheme-handler/prusaslicer;\n"
        "StartupNotify=false\n"
        "NoDisplay=true\n"
        , name_suffix, excutable_path);

    // desktop file for downloader as part of main app
    std::string desktop_path = GUI::format("%1%/applications/PrusaSlicerURLProtocol%2%.desktop", target_dir_desktop, version_suffix);
    if (create_desktop_file(desktop_path, desktop_file_downloader)) {
        // save path to desktop file
        app_config->set("desktop_integration_URL_path", desktop_path);
        // finish registration on mime type
        std::string command = GUI::format("xdg-mime default PrusaSlicerURLProtocol%1%.desktop x-scheme-handler/prusaslicer", version_suffix);
        BOOST_LOG_TRIVIAL(debug) << "system command: " << command;
        int r = system(command.c_str());
        BOOST_LOG_TRIVIAL(debug) << "system result: " << r;

    }

    bool candidate_found = false;
    for (size_t i = 0; i < target_candidates.size(); ++i) {
        if (contains_path_dir(target_candidates[i], "applications")) {
            target_dir_desktop = target_candidates[i];
            // Write slicer desktop file
            std::string path = GUI::format("%1%/applications/PrusaSlicerURLProtocol%2%.desktop", target_dir_desktop, version_suffix);
            if (create_desktop_file(path, desktop_file_downloader)) {
                app_config->set("desktop_integration_URL_path", path);
                candidate_found = true;
                BOOST_LOG_TRIVIAL(debug) << "PrusaSlicerURLProtocol.desktop file installation success.";
                break;
            }
            else {
                // write failed - try another path
                BOOST_LOG_TRIVIAL(debug) << "Attempt to PrusaSlicerURLProtocol.desktop file installation failed. failed path: " << target_candidates[i];
                target_dir_desktop.clear();
            }
        }
    }
    // if all failed - try creating default home folder
    if (!candidate_found) {
        // create $HOME/.local/share
        create_path(into_u8(wxFileName::GetHomeDir()), ".local/share/applications");
        // create desktop file
        target_dir_desktop = GUI::format("%1%/.local/share", wxFileName::GetHomeDir());
        std::string path = GUI::format("%1%/applications/PrusaSlicerURLProtocol%2%.desktop", target_dir_desktop, version_suffix);
        if (contains_path_dir(target_dir_desktop, "applications")) {
            if (!create_desktop_file(path, desktop_file_downloader)) {
                // Desktop file not written - end desktop integration
                BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed - could not create desktop file.";
                return;
            }
            app_config->set("desktop_integration_URL_path", path);
        }
        else {
            // Desktop file not written - end desktop integration
            BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed because the application directory was not found.";
            return;
        }
    }
    assert(!target_dir_desktop.empty());
    if (target_dir_desktop.empty()) {
        // Desktop file not written - end desktop integration
        BOOST_LOG_TRIVIAL(error) << "Performing downloader desktop integration failed because the application directory was not found.";
        show_error(nullptr, _L("Performing downloader desktop integration failed because the application directory was not found."));
        return;
    }

    // finish registration on mime type
    std::string command = GUI::format("xdg-mime default PrusaSlicerURLProtocol%1%.desktop x-scheme-handler/prusaslicer", version_suffix);
    BOOST_LOG_TRIVIAL(debug) << "system command: " << command;
    int r = system(command.c_str());
    BOOST_LOG_TRIVIAL(debug) << "system result: " << r;

    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::DesktopIntegrationSuccess);
}
void DesktopIntegrationDialog::undo_downloader_registration()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    const AppConfig *app_config = wxGetApp().app_config;
    std::string path = std::string(app_config->get("desktop_integration_URL_path"));
    if (!path.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "removing " << path;
        std::remove(path.c_str());
    }
    // There is no need to undo xdg-mime default command. It is done automatically when desktop file is deleted.
}
void DesktopIntegrationDialog::undo_downloader_registration_rigid()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    // Try ro find any PrusaSlicerURLProtocol.desktop files including alpha and beta and get rid of them

    // $XDG_DATA_HOME defines the base directory relative to which user specific data files should be stored.
    // If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used.
    // $XDG_DATA_DIRS defines the preference-ordered set of base directories to search for data files in addition to the $XDG_DATA_HOME base directory.
    // The directories in $XDG_DATA_DIRS should be seperated with a colon ':'.
    // If $XDG_DATA_DIRS is either not set or empty, a value equal to /usr/local/share/:/usr/share/ should be used.
    std::vector<std::string>target_candidates;
    target_candidates.emplace_back(GUI::into_u8(wxFileName::GetHomeDir()) + "/.local/share");
    resolve_path_from_var("XDG_DATA_HOME", target_candidates);
    resolve_path_from_var("XDG_DATA_DIRS", target_candidates);
    for (const std::string& cand : target_candidates) {
        boost::filesystem::path apps_path = get_existing_dir(cand, "applications");
        if (apps_path.empty()) {
            continue;
        }
        for (const std::string& suffix : {"" , "-beta", "-alpha" , "_beta", "_alpha"}) {
            boost::filesystem::path file_path = apps_path / GUI::format("PrusaSlicerURLProtocol%1%.desktop", suffix);
            boost::system::error_code ec;
            if (!boost::filesystem::exists(file_path, ec) || ec) {
                continue;
            }
            if (!boost::filesystem::remove(file_path, ec) || ec) {
                BOOST_LOG_TRIVIAL(error) << "Failed to remove file " << file_path << " ec: " << ec.message();
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "Desktop File removed: " << file_path;
        }
    }
}

void DesktopIntegrationDialog::find_all_desktop_files(std::vector<boost::filesystem::path>& results)
{
    // Try ro find any PrusaSlicer.desktop and PrusaSlicerGcodeViewer.desktop and PrusaSlicerURLProtocol.desktop files including alpha and beta

    // For regular apps (f.e. appimage) this is true:
    // $XDG_DATA_HOME defines the base directory relative to which user specific data files should be stored.
    // If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used.
    // $XDG_DATA_DIRS defines the preference-ordered set of base directories to search for data files in addition to the $XDG_DATA_HOME base directory.
    // The directories in $XDG_DATA_DIRS should be seperated with a colon ':'.
    // If $XDG_DATA_DIRS is either not set or empty, a value equal to /usr/local/share/:/usr/share/ should be used.

    // But flatpak resets XDG_DATA_HOME and XDG_DATA_DIRS, so we do not look into them
    // Lets look into $HOME/.local/share, /usr/local/share/, /usr/share/
    std::vector<std::string> target_candidates;
    if (auto home_config_dir = Slic3r::get_home_local_dir(); home_config_dir) {
        target_candidates.emplace_back((*home_config_dir).string() + "/share");
    }
    target_candidates.emplace_back("usr/local/share/");
    target_candidates.emplace_back("usr/share/");
    for (const std::string& cand : target_candidates) {
        boost::filesystem::path apps_path = get_existing_dir(cand, "applications");
        if (apps_path.empty()) {
            continue;
        }
        for (const std::string& filename : {"PrusaSlicer","PrusaSlicerGcodeViewer","PrusaSlicerURLProtocol"}) {
            for (const std::string& suffix : {"" , "-beta", "-alpha", "_beta", "_alpha"}) {
                boost::filesystem::path file_path = apps_path / GUI::format("%1%%2%.desktop", filename, suffix);
                boost::system::error_code ec;
                if (!boost::filesystem::exists(file_path, ec) || ec) {
                    continue;
                }
                BOOST_LOG_TRIVIAL(debug) << "Desktop File found: " << file_path;
                results.emplace_back(std::move(file_path));
            }
        }
    }
}

void DesktopIntegrationDialog::remove_desktop_file_list(const std::vector<boost::filesystem::path>& list, std::vector<boost::filesystem::path>& fails)
{
    for (const boost::filesystem::path& entry : list) {
        boost::system::error_code ec;
        if (!boost::filesystem::remove(entry, ec) || ec) {
            BOOST_LOG_TRIVIAL(error) << "Failed to remove file " << entry << " ec: " << ec.message();
            fails.emplace_back(entry);
            continue;
        }
        BOOST_LOG_TRIVIAL(info) << "Desktop File removed: " << entry;
    }
}

DesktopIntegrationDialog::DesktopIntegrationDialog(wxWindow *parent)
: wxDialog(parent, wxID_ANY, _(L("Desktop Integration")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	bool can_undo = DesktopIntegrationDialog::is_integrated();

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);


	wxString text = _L("Desktop Integration sets this binary to be searchable by the system.\n\nPress \"Perform\" to proceed.");
	if (can_undo)
		text += "\nPress \"Undo\" to remove previous integration.";

    vbox->Add(
        new wxStaticText( this, wxID_ANY, text),
        //	, wxDefaultPosition, wxSize(100,50), wxTE_MULTILINE),
        1,            // make vertically stretchable
        wxEXPAND |    // make horizontally stretchable
        wxALL,        //   and make border all around
        10 );         // set border width to 10


	wxBoxSizer *btn_szr = new wxBoxSizer(wxHORIZONTAL);
	wxButton *btn_perform = new wxButton(this, wxID_ANY, _L("Perform"));
	btn_szr->Add(btn_perform, 0, wxALL, 10);

	btn_perform->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::perform_desktop_integration(); EndModal(wxID_ANY); });

	if (can_undo){
		wxButton *btn_undo = new wxButton(this, wxID_ANY, _L("Undo"));
		btn_szr->Add(btn_undo, 0, wxALL, 10);
		btn_undo->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { DesktopIntegrationDialog::undo_desktop_integration(); EndModal(wxID_ANY); });
	}
	wxButton *btn_cancel = new wxButton(this, wxID_ANY, _L("Cancel"));
	btn_szr->Add(btn_cancel, 0, wxALL, 10);
	btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_ANY); });

	vbox->Add(btn_szr, 0, wxALIGN_CENTER);

    SetSizerAndFit(vbox);
}

DesktopIntegrationDialog::~DesktopIntegrationDialog()
{
}

} // namespace GUI
} // namespace Slic3r
#endif // __linux__
