//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameImporter.cpp
//
//  Game import utility.
//

#include "guis/GuiGameImporter.h"

#include "Log.h"
#include "resources/Font.h"

#include <SDL2/SDL_timer.h>

#if defined(__ANDROID__)
#include "utils/PlatformUtilAndroid.h"
#endif

#define CHECKED_PATH ":/graphics/checkbox_checked.svg"
#define UNCHECKED_PATH ":/graphics/checkbox_unchecked.svg"

GuiGameImporter::GuiGameImporter(std::string title)
    : mRenderer {Renderer::getInstance()}
    , mMenu {title}
    , mNoConfig {false}
    , mSelectorWindow {false}
    , mHasUpdates {false}
    , mAndroidGetApps {false}
    , mIsImporting {false}
    , mDoneImporting {false}
    , mHasEntries {false}
{
    mTempDir = Utils::FileSystem::getAppDataDirectory() + "/importer_temp";
    Utils::FileSystem::removeDirectory(mTempDir, true);

    mTargetSystem =
        std::make_shared<OptionListComponent<std::string>>(_("IMPORT TO SYSTEM"), false);

    std::string selectedSystem {Settings::getInstance()->getString("ImporterTargetSystem")};

    for (auto& importRule : SystemData::sImportRules.get()->mSystems) {
        if (!importRule.second.validSystem) {
            LOG(LogWarning) << "GuiGameImporter: Skipping configuration entry for invalid system \""
                            << importRule.first << "\"";
            continue;
        }
        mTargetSystem->add(Utils::String::toUpper(importRule.second.fullName), importRule.first,
                           selectedSystem == importRule.first);
    }

    mMenu.addSaveFunc([this] {
        if (mTargetSystem->getSelected() !=
            Settings::getInstance()->getString("ImporterTargetSystem")) {
            Settings::getInstance()->setString("ImporterTargetSystem",
                                               mTargetSystem->getSelected());
            mMenu.setNeedsSaving();
        }
    });

    if (mTargetSystem->getNumEntries() == 0) {
        mTargetSystem->add(_("NO CONFIGURATION"), "noconfig", selectedSystem == "noconfig");
        mNoConfig = true;
    }
    else if (mTargetSystem->getSelectedObjects().size() == 0) {
        mTargetSystem->selectEntry(0);
    }

    mMenu.addWithLabel(_("IMPORT TO SYSTEM"), mTargetSystem);

    mRemoveEntries = std::make_shared<OptionListComponent<std::string>>(_("REMOVE ENTRIES"), false);
    std::string selectedRemoveEntries {Settings::getInstance()->getString("ImporterRemoveEntries")};
    mRemoveEntries->add(_("NEVER"), "never", selectedRemoveEntries == "never");
    mRemoveEntries->add(_("ALL UNSELECTED"), "unselected", selectedRemoveEntries == "unselected");
    mMenu.addSaveFunc([this] {
        if (mRemoveEntries->getSelected() !=
            Settings::getInstance()->getString("ImporterRemoveEntries")) {
            Settings::getInstance()->setString("ImporterRemoveEntries",
                                               mRemoveEntries->getSelected());
            mMenu.setNeedsSaving();
        }
    });

    if (mRemoveEntries->getSelectedObjects().size() == 0)
        mRemoveEntries->selectEntry(0);

    mMenu.addWithLabel(_("REMOVE ENTRIES"), mRemoveEntries);

    mMediaTarget =
        std::make_shared<OptionListComponent<std::string>>(_("MEDIA TARGET TYPE"), false);
    std::string selectedMediaTarget {Settings::getInstance()->getString("ImporterMediaTarget")};
    mMediaTarget->add(_("SCREENSHOTS"), "screenshots", selectedMediaTarget == "screenshots");
    mMediaTarget->add(_("TITLE SCREENS"), "titlescreens", selectedMediaTarget == "titlescreens");
    mMediaTarget->add(_("COVERS"), "covers", selectedMediaTarget == "covers");
    mMediaTarget->add(_("BACK COVERS"), "backcovers", selectedMediaTarget == "backcovers");
    mMediaTarget->add(_("MARQUEES (WHEELS)"), "marquees", selectedMediaTarget == "marquees");
    mMediaTarget->add(_("3D BOXES"), "3dboxes", selectedMediaTarget == "3dboxes");
    mMediaTarget->add(_("PHYSICAL MEDIA"), "physicalmedia", selectedMediaTarget == "physicalmedia");
    mMediaTarget->add(_("FAN ART"), "fanart", selectedMediaTarget == "fanart");
    mMenu.addSaveFunc([this] {
        if (mMediaTarget->getSelected() !=
            Settings::getInstance()->getString("ImporterMediaTarget")) {
            Settings::getInstance()->setString("ImporterMediaTarget", mMediaTarget->getSelected());
            mMenu.setNeedsSaving();
        }
    });

    if (mMediaTarget->getSelectedObjects().size() == 0)
        mMediaTarget->selectEntry(0);

    mMenu.addWithLabel(_("MEDIA TARGET TYPE"), mMediaTarget);

    mImportMedia = std::make_shared<SwitchComponent>();
    mImportMedia->setState(Settings::getInstance()->getBool("ImporterImportMedia"));
    mMenu.addWithLabel(_("IMPORT MEDIA"), mImportMedia);
    mMenu.addSaveFunc([this] {
        if (mImportMedia->getState() != Settings::getInstance()->getBool("ImporterImportMedia")) {
            Settings::getInstance()->setBool("ImporterImportMedia", mImportMedia->getState());
            mMenu.setNeedsSaving();
        }
    });

    mImportMediaAdditional = std::make_shared<SwitchComponent>();
    mImportMediaAdditional->setState(
        Settings::getInstance()->getBool("ImporterImportMediaAdditional"));
    mMenu.addWithLabel(_("IMPORT BANNER OR LOGO IF AVAILABLE"), mImportMediaAdditional);
    mMenu.addSaveFunc([this] {
        if (mImportMediaAdditional->getState() !=
            Settings::getInstance()->getBool("ImporterImportMediaAdditional")) {
            Settings::getInstance()->setBool("ImporterImportMediaAdditional",
                                             mImportMediaAdditional->getState());
            mMenu.setNeedsSaving();
        }
    });

    auto importMediaToggleFunc = [this]() {
        if (mImportMedia->getState() == false) {
            mMediaTarget->setEnabled(false);
            mMediaTarget->setOpacity(DISABLED_OPACITY);
            mMediaTarget->getParent()
                ->getChild(mMediaTarget->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);

            mImportMediaAdditional->setEnabled(false);
            mImportMediaAdditional->setOpacity(DISABLED_OPACITY);
            mImportMediaAdditional->getParent()
                ->getChild(mImportMediaAdditional->getChildIndex() - 1)
                ->setOpacity(DISABLED_OPACITY);
        }
        else {
            mMediaTarget->setEnabled(true);
            mMediaTarget->setOpacity(1.0f);
            mMediaTarget->getParent()
                ->getChild(mMediaTarget->getChildIndex() - 1)
                ->setOpacity(1.0f);

            mImportMediaAdditional->setEnabled(true);
            mImportMediaAdditional->setOpacity(1.0f);
            mImportMediaAdditional->getParent()
                ->getChild(mImportMediaAdditional->getChildIndex() - 1)
                ->setOpacity(1.0f);
        }
    };

    importMediaToggleFunc();
    mImportMedia->setCallback(importMediaToggleFunc);

    mGamesOnly = std::make_shared<SwitchComponent>();
    mGamesOnly->setState(Settings::getInstance()->getBool("ImporterGamesOnly"));
    mMenu.addWithLabel(_("ONLY INCLUDE APPS CATEGORIZED AS GAMES"), mGamesOnly);
    mMenu.addSaveFunc([this] {
        if (mGamesOnly->getState() != Settings::getInstance()->getBool("ImporterGamesOnly")) {
            Settings::getInstance()->setBool("ImporterGamesOnly", mGamesOnly->getState());
            mMenu.setNeedsSaving();
        }
    });

    mMenu.addButton(_("START"), _("start importer"),
                    std::bind(&GuiGameImporter::pressedStart, this));
    mMenu.addButton(_("BACK"), _("back"), [&] { delete this; });

    if (mNoConfig) {
        mTargetSystem->selectEntry(0);
        mTargetSystem->setEnabled(false);
        mTargetSystem->setOpacity(DISABLED_OPACITY);
        mTargetSystem->getParent()
            ->getChild(mTargetSystem->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mRemoveEntries->setEnabled(false);
        mRemoveEntries->setOpacity(DISABLED_OPACITY);
        mRemoveEntries->getParent()
            ->getChild(mRemoveEntries->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mMediaTarget->setEnabled(false);
        mMediaTarget->setOpacity(DISABLED_OPACITY);
        mMediaTarget->getParent()
            ->getChild(mMediaTarget->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mImportMedia->setEnabled(false);
        mImportMedia->setOpacity(DISABLED_OPACITY);
        mImportMedia->getParent()
            ->getChild(mImportMedia->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mImportMediaAdditional->setEnabled(false);
        mImportMediaAdditional->setOpacity(DISABLED_OPACITY);
        mImportMediaAdditional->getParent()
            ->getChild(mImportMediaAdditional->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mGamesOnly->setEnabled(false);
        mGamesOnly->setOpacity(DISABLED_OPACITY);
        mGamesOnly->getParent()
            ->getChild(mGamesOnly->getChildIndex() - 1)
            ->setOpacity(DISABLED_OPACITY);

        mMenu.setButtonOpacity(0, 0.5f);
    }

    setSize(mMenu.getSize());

    setPosition((mRenderer->getScreenWidth() - mSize.x) / 2.0f,
                mRenderer->getScreenHeight() * 0.13f);

    mBusyAnim.setSize(mSize);
    mBusyAnim.setText(_("WORKING..."));
    mBusyAnim.onSizeChanged();

    mainWindow();
}

GuiGameImporter::~GuiGameImporter()
{
    mIsImporting = false;

    if (mImportThread) {
        mImportThread->join();
        mImportThread.reset();
    }

    Utils::FileSystem::removeDirectory(mTempDir, true);

    if (mHasUpdates) {
        ViewController::getInstance()->rescanROMDirectory();
        while (mWindow->getGuiStackSize() > 1)
            mWindow->removeGui(mWindow->peekGui());
    }
}

void GuiGameImporter::update(int deltaTime)
{
    if (mIsImporting)
        mBusyAnim.update(deltaTime);

    if (mAndroidGetApps && mIsImporting) {
        // We call the Android retrieval function here instead of in pressedStart() to be able
        // to render a static busy indicator before executing the call.
        mAndroidGetApps = false;
        mIsImporting = true;
        std::vector<std::pair<std::string, std::string>> appList;
#if defined(__ANDROID__)
        Utils::Platform::Android::getInstalledApps(appList, mGamesOnly->getState(),
                                                   mImportMediaAdditional->getState());
#endif
        mImportThread =
            std::make_unique<std::thread>(&GuiGameImporter::androidpackageRule, this, appList);
    }

    if (mDoneImporting) {
        mIsImporting = false;
        mDoneImporting = false;
        if (mHasEntries) {
            mHasEntries = false;
            selectorWindow();
        }
        else {
            mWindow->pushGui(new GuiMsgBox(
                _("COULDN'T FIND ANYTHING TO IMPORT"), _("OK"), [] {}, "", nullptr, "", nullptr, "",
                nullptr, nullptr, true, true));
        }
    }

    GuiComponent::update(deltaTime);
}

void GuiGameImporter::render(const glm::mat4& parentTrans)
{
    glm::mat4 trans {parentTrans * getTransform()};
    renderChildren(trans);

    if (mIsImporting || mAndroidGetApps)
        mBusyAnim.render(trans);

    if (mAndroidGetApps)
        mIsImporting = true;
}

std::vector<HelpPrompt> GuiGameImporter::getHelpPrompts()
{
    if (mSelectorWindow) {
        std::vector<HelpPrompt> prompts {mSelectorMenu->getHelpPrompts()};
        prompts.push_back(HelpPrompt("b", _("cancel")));
        return prompts;
    }
    else {
        std::vector<HelpPrompt> prompts {mMenu.getHelpPrompts()};
        prompts.push_back(HelpPrompt("b", _("back")));
        prompts.push_back(HelpPrompt("y", _("start importer")));
        return prompts;
    }
}

void GuiGameImporter::pressedStart()
{
    if (mNoConfig)
        return;

    Utils::FileSystem::removeDirectory(mTempDir, true);
    mHasUpdates = false;

    mTargetSystemDir = mTargetSystem->getSelected();
    mMediaTargetDir = mMediaTarget->getSelected();
    mFileExtension = "";

    for (auto& importRule : SystemData::sImportRules.get()->mSystems) {
        if (importRule.first == mTargetSystemDir) {
            mFileExtension = importRule.second.extension;
            break;
        }
    }

    auto importFunc = [this]() {
        if (mImportThread) {
            mImportThread->join();
            mImportThread.reset();
        }

        mMediaFileExtension = "";

        for (auto& importRule : SystemData::sImportRules.get()->mSystems) {
            if (importRule.first == mTargetSystemDir) {
                if (importRule.second.ruleType == "androidpackage") {
                    mMediaFileExtension = ".png";
                    std::vector<std::pair<std::string, std::string>> appList;
                    // Due to JNI weirdness on Android where there are issues with SDL if
                    // attempting to run the app retrieval in a separate thread we instead need
                    // to run this on the main thread. We set a flag to execute it from the
                    // update() function which is just a hack to make sure a static busy
                    // indicator is rendered before calling the retrieval function.
                    mAndroidGetApps = true;
                }
                else if (importRule.second.ruleType == "files") {
                    mImportThread =
                        std::make_unique<std::thread>(&GuiGameImporter::filesRule, this);
                }
                break;
            }
        }
    };

    if (mRemoveEntries->getSelected() == "unselected") {
        mWindow->pushGui(new GuiMsgBox(
            Utils::String::format(
                _("YOU HAVE CHOSEN TO REMOVE ALL UNSELECTED ENTRIES, THIS WILL DELETE "
                  "ALL GAME FILES WITH THE \"%s\" FILE EXTENSION FROM THE \"%s\" SYSTEM DIRECTORY "
                  "AND THEN IMPORT THE ENTRIES YOU SELECT ON THE NEXT SCREEN\nARE YOU SURE?"),
                mFileExtension.c_str(), mTargetSystem->getSelected().c_str()),
            _("YES"), [importFunc] { importFunc(); }, "NO", nullptr, "", nullptr, "", nullptr,
            nullptr, false, true,
            (mRenderer->getIsVerticalOrientation() ?
                 0.94f :
                 0.60f * (1.778f / mRenderer->getScreenAspectRatio()))));
    }
    else {
        importFunc();
    }
}

void GuiGameImporter::mainWindow()
{
    mSelectorMenu.reset();
    mSelectorWindow = false;
    addChild(&mMenu);
    mWindow->setHelpPrompts(getHelpPrompts());
}

void GuiGameImporter::selectorWindow()
{
    // We call this just to reset the busy indicator to the first animation frame, in case
    // the selector window is closed and the import is initialized again.
    mBusyAnim.onSizeChanged();

    removeChild(&mMenu);

    mSelectorMenu = std::make_unique<MenuComponent>(_("MAKE YOUR SELECTION"));
    addChild(mSelectorMenu.get());

    mCheckboxes.clear();
    ComponentListRow row;

    auto spacer = std::make_shared<GuiComponent>();
    spacer->setSize(mRenderer->getScreenWidth() * 0.005f, 0.0f);

    std::string imagePath;
    std::list<std::string> inputFileList {Utils::FileSystem::getDirContent(mTempDir + "/files")};

    // Always use case-insensitive sorting of the actual filename exluding its path and extension.
    inputFileList.sort([](std::string a, std::string b) {
        std::string aFile {Utils::FileSystem::getStem(Utils::FileSystem::getFileName(a))};
        std::string bFile {Utils::FileSystem::getStem(Utils::FileSystem::getFileName(b))};
        return Utils::String::toUpper(aFile) < Utils::String::toUpper(bFile);
    });

    LOG(LogDebug) << "GuiGameImporter::selectorWindow(): Retrieved " << inputFileList.size()
                  << (inputFileList.size() == 1 ? " entry" : " entries") << " for system \""
                  << mTargetSystemDir << "\"";

    std::vector<std::pair<std::string, std::string>> fileList;

    const bool darkColorScheme {Settings::getInstance()->getString("MenuColorScheme") != "light"};

    for (std::string& file : inputFileList) {
        std::string mediaFile {mTempDir + "/icons/" +
                               Utils::FileSystem::getStem(Utils::FileSystem::getFileName(file)) +
                               mMediaFileExtension};

        row.elements.clear();
        auto lbl = std::make_shared<TextComponent>(
            Utils::FileSystem::getStem(Utils::FileSystem::getFileName(file)),
            Font::get(FONT_SIZE_MEDIUM), mMenuColorPrimary);

        auto media = std::make_shared<ImageComponent>();
        media->setResize(0, Font::get(FONT_SIZE_MEDIUM)->getLetterHeight() * 1.4f);
        if (!darkColorScheme)
            media->setInvertInMenus(false);

        std::string mediaFileAdditional;

        if (mImportMediaAdditional->getState()) {
            mediaFileAdditional = mTempDir + "/media/" +
                                  Utils::FileSystem::getStem(Utils::FileSystem::getFileName(file)) +
                                  mMediaFileExtension;
        }

        if (Utils::FileSystem::exists(mediaFile)) {
            fileList.emplace_back(std::make_pair(
                file, (Utils::FileSystem::exists(mediaFileAdditional) ? mediaFileAdditional :
                                                                        mediaFile)));
            media->setImage(mediaFile);
        }
        else {
            fileList.emplace_back(std::make_pair(file, ""));
        }

        auto checkbox = std::make_shared<ImageComponent>();
        checkbox->setResize(0, Font::get(FONT_SIZE_MEDIUM)->getLetterHeight());
        checkbox->setImage(UNCHECKED_PATH);
        checkbox->setColorShift(mMenuColorPrimary);
        checkbox->setEnabled(false);
        mCheckboxes.emplace_back(checkbox);

        row.addElement(media, false);
        row.addElement(spacer, false);
        row.addElement(lbl, true);
        row.addElement(checkbox, false);
        row.makeAcceptInputHandler([checkbox] {
            if (checkbox->getEnabled()) {
                checkbox->setEnabled(false);
                checkbox->setImage(UNCHECKED_PATH);
            }
            else {
                checkbox->setEnabled(true);
                checkbox->setImage(CHECKED_PATH);
            }
        });
        mSelectorMenu->addRow(row);
    }

    mSelectorMenu->addButton(_("IMPORT"), _("import"), [this, fileList] {
        const std::string removeEntries {mRemoveEntries->getSelected()};
        const bool importMedia {mImportMedia->getState()};
        int numEntriesImported {0};

        if (removeEntries == "unselected") {
            for (auto& file : Utils::FileSystem::getDirContent(
                     FileData::getROMDirectory() + mTargetSystemDir, false)) {
                if (Utils::FileSystem::getExtension(file) == mFileExtension) {
                    LOG(LogInfo) << "GuiGameImporter: Removed file \"" << file << "\"";
                    Utils::FileSystem::removeFile(file);
                }
            }
        }

        for (int i {0}; i < static_cast<int>(mCheckboxes.size()); ++i) {
            const std::string systemDir {FileData::getROMDirectory() + mTargetSystemDir};
            const std::string mediaDir {FileData::getMediaDirectory() + mTargetSystemDir + "/" +
                                        mMediaTargetDir};

            if (!Utils::FileSystem::exists(systemDir))
                Utils::FileSystem::createDirectory(systemDir);

            if (!Utils::FileSystem::exists(systemDir))
                return;

            if (importMedia) {
                if (!Utils::FileSystem::exists(mediaDir))
                    Utils::FileSystem::createDirectory(mediaDir);

                if (!Utils::FileSystem::exists(mediaDir))
                    return;
            }

            if (mCheckboxes[i]->getEnabled()) {
                mHasUpdates = true;
                ++numEntriesImported;

                const std::string file {fileList[i].first};
                if (Utils::FileSystem::exists(file)) {
                    // We have to copy and not rename the files as they may need to move across
                    // different storage devices.
                    Utils::FileSystem::copyFile(
                        file, systemDir + "/" + Utils::FileSystem::getFileName(file), true);
                    Utils::FileSystem::removeFile(file);

                    LOG(LogDebug) << "GuiGameImporter::selectorWindow(): Importing \""
                                  << Utils::FileSystem::getStem(
                                         Utils::FileSystem::getFileName(file))
                                  << "\"";

                    const std::string mediaFile {fileList[i].second};

                    if (importMedia) {
                        if (Utils::FileSystem::exists(mediaFile)) {
                            Utils::FileSystem::copyFile(
                                mediaFile,
                                mediaDir + "/" + Utils::FileSystem::getFileName(mediaFile), true);
                            Utils::FileSystem::removeFile(mediaFile);
                        }
                    }
                }
            }
        }

        if (mHasUpdates) {
            LOG(LogInfo) << "GuiGameImporter: Imported " << numEntriesImported
                         << (numEntriesImported == 1 ? " entry" : " entries") << " for system \""
                         << mTargetSystemDir << "\"";
            delete this;
        }
    });

    mSelectorMenu->addButton(_("CANCEL"), _("cancel"), [this] {
        removeChild(mSelectorMenu.get());
        mainWindow();
    });
    mSelectorMenu->addButton(_("SELECT ALL"), _("select all"), [this] {
        for (auto& checkbox : mCheckboxes) {
            checkbox->setEnabled(true);
            checkbox->setImage(CHECKED_PATH);
        }
    });

    mSelectorMenu->addButton(_("SELECT NONE"), _("select none"), [this] {
        for (auto& checkbox : mCheckboxes) {
            checkbox->setEnabled(false);
            checkbox->setImage(UNCHECKED_PATH);
        }
    });

    mSelectorWindow = true;
    mWindow->setHelpPrompts(getHelpPrompts());
}

void GuiGameImporter::androidpackageRule(std::vector<std::pair<std::string, std::string>> appList)
{
    mIsImporting = true;

    // This is just so that the busy component gets shown briefly regardless of processing time.
    SDL_Delay(400);

#if defined(__ANDROID__)
    if (appList.size() > 0) {
        const std::string filesDir {mTempDir + "/files"};
        if (!Utils::FileSystem::exists(filesDir))
            Utils::FileSystem::createDirectory(filesDir);
        if (!Utils::FileSystem::exists(filesDir)) {
            LOG(LogError) << "Couldn't create temporary files directory";
            return;
        }
    }

    for (auto& app : appList) {
        mHasEntries = true;
        std::ofstream appFile;
        appFile.open(mTempDir + "/files/" + app.first + mFileExtension, std::ios::binary);
        appFile << app.second << std::endl;
        appFile.close();
    }
#endif

    mIsImporting = false;
    mDoneImporting = true;
}

void GuiGameImporter::filesRule()
{
    mIsImporting = true;
    SDL_Delay(700);
    mIsImporting = false;
    mHasEntries = true;
    mDoneImporting = true;
}

bool GuiGameImporter::input(InputConfig* config, Input input)
{
    if (mIsImporting)
        return true;

    if (GuiComponent::input(config, input))
        return true;

    if (!mSelectorWindow) {
        if (config->isMappedTo("y", input) && input.value != 0)
            pressedStart();

        if (input.value != 0 &&
            (config->isMappedTo("b", input) || config->isMappedTo("back", input))) {
            delete this;
            return true;
        }
    }
    else {
        if (input.value != 0 &&
            (config->isMappedTo("b", input) || config->isMappedTo("back", input))) {
            removeChild(mSelectorMenu.get());
            mainWindow();
            return true;
        }
    }

    return GuiComponent::input(config, input);
}
