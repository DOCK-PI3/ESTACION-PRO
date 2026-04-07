//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  ThreadedScraper.cpp
//
//  Non-interactive multi-scraper running in the background while users keep navigating.
//

#include "scrapers/ThreadedScraper.h"

#include "CollectionSystemsManager.h"
#include "FileFilterIndex.h"
#include "GamelistFileParser.h"
#include "Scripting.h"
#include "Settings.h"
#include "SystemData.h"
#include "Window.h"
#include "guis/GuiScraperSearch.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <sstream>

std::unique_ptr<ThreadedScraper> ThreadedScraper::sInstance {nullptr};

void ThreadedScraper::start(
    const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches)
{
    if (sInstance)
        return;

    if (searches.first.empty())
        return;

    sInstance = std::unique_ptr<ThreadedScraper>(new ThreadedScraper(searches));
}

void ThreadedScraper::stop()
{
    if (!sInstance)
        return;

    sInstance->finish(true);
    sInstance.reset();
}

bool ThreadedScraper::isRunning()
{
    return static_cast<bool>(sInstance);
}

void ThreadedScraper::update()
{
    if (!sInstance)
        return;

    ThreadedScraper& instance {*sInstance};

    if (instance.mSearchHandle && instance.mSearchHandle->status() != ASYNC_IN_PROGRESS) {
        const auto status {instance.mSearchHandle->status()};
        const auto results {instance.mSearchHandle->getResults()};
        const std::string statusString {instance.mSearchHandle->getStatusString()};
        instance.mSearchHandle.reset();

        if (status == ASYNC_DONE)
            instance.processSearchDone(results);
        else
            instance.processSearchError(statusString);

        return;
    }

    if (instance.mMDRetrieveURLsHandle &&
        instance.mMDRetrieveURLsHandle->status() != ASYNC_IN_PROGRESS) {
        const auto status {instance.mMDRetrieveURLsHandle->status()};
        const auto results {instance.mMDRetrieveURLsHandle->getResults()};
        const std::string statusString {instance.mMDRetrieveURLsHandle->getStatusString()};
        instance.mMDRetrieveURLsHandle.reset();

        if (status == ASYNC_DONE)
            instance.processMediaURLsDone(results);
        else
            instance.processSearchError(statusString);

        return;
    }

    if (instance.mMDResolveHandle && instance.mMDResolveHandle->status() != ASYNC_IN_PROGRESS) {
        const auto status {instance.mMDResolveHandle->status()};
        const auto statusString {instance.mMDResolveHandle->getStatusString()};

        if (status == ASYNC_DONE) {
            const auto result {instance.mMDResolveHandle->getResult()};
            instance.mSavedNewMedia =
                instance.mSavedNewMedia || instance.mMDResolveHandle->getSavedNewMedia();
            instance.mMDResolveHandle.reset();
            instance.acceptResult(result);
        }
        else {
            instance.mMDResolveHandle.reset();
            instance.processSearchError(statusString);
        }

        return;
    }

    if (!instance.mHasCurrentSearch && !instance.mSearchQueue.empty()) {
        instance.doNextSearch();
        return;
    }

    if (!instance.mHasCurrentSearch && instance.mSearchQueue.empty()) {
        instance.finish(false);
        sInstance.reset();
    }
}

ThreadedScraper::ThreadedScraper(
    const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches)
    : mSearchQueue {searches.first}
    , mQueueCountPerSystem {searches.second}
    , mHasCurrentSearch {false}
    , mTotalGames {static_cast<int>(searches.first.size())}
    , mCurrentGame {0}
    , mTotalSuccessful {0}
    , mTotalSkipped {0}
    , mSavedNewMedia {false}
{
    std::string scraperService;
    if (Settings::getInstance()->getString("Scraper") == "thegamesdb")
        scraperService = "thegamesdb";
    else
        scraperService = "screenscraper";

    Scripting::fireEvent("scraper-start", scraperService, "automatic",
                         std::to_string(mQueueCountPerSystem.size()), std::to_string(mTotalGames));

    updateProgressPopup();
}

void ThreadedScraper::doNextSearch()
{
    if (mSearchQueue.empty())
        return;

    mCurrentSearch = mSearchQueue.front();
    mHasCurrentSearch = true;
    mCurrentSearch.automaticMode = true;

    mSearchHandle = startScraperSearch(mCurrentSearch);
}

void ThreadedScraper::processSearchDone(const std::vector<ScraperSearchResult>& results)
{
    if (results.empty()) {
        skipCurrent();
        return;
    }

    if (results.front().mediaURLFetch == COMPLETED) {
        mMDResolveHandle = resolveMetaDataAssets(results.front(), mCurrentSearch);
        return;
    }

    mPendingResults = results;

    std::string gameIDs;
    for (const auto& result : results)
        gameIDs += result.gameID + ',';

    if (gameIDs.empty()) {
        skipCurrent();
        return;
    }

    gameIDs.pop_back();
    mMDRetrieveURLsHandle = startMediaURLsFetch(gameIDs);
}

void ThreadedScraper::processSearchError(const std::string& errorText)
{
    (void)errorText;
    skipCurrent();
}

void ThreadedScraper::processMediaURLsDone(const std::vector<ScraperSearchResult>& mediaResults)
{
    if (mediaResults.empty() || mPendingResults.empty()) {
        skipCurrent();
        return;
    }

    // Merge media URLs into the primary scrape results so we keep metadata fields.
    for (const auto& mediaResult : mediaResults) {
        for (auto& pendingResult : mPendingResults) {
            if (pendingResult.gameID == mediaResult.gameID) {
                pendingResult.box3DUrl = mediaResult.box3DUrl;
                pendingResult.backcoverUrl = mediaResult.backcoverUrl;
                pendingResult.coverUrl = mediaResult.coverUrl;
                pendingResult.fanartUrl = mediaResult.fanartUrl;
                pendingResult.marqueeUrl = mediaResult.marqueeUrl;
                pendingResult.screenshotUrl = mediaResult.screenshotUrl;
                pendingResult.titlescreenUrl = mediaResult.titlescreenUrl;
                pendingResult.physicalmediaUrl = mediaResult.physicalmediaUrl;
                pendingResult.videoUrl = mediaResult.videoUrl;
                pendingResult.manualUrl = mediaResult.manualUrl;
                pendingResult.mediaURLFetch = COMPLETED;
            }
        }
    }

    if (mPendingResults.front().mediaURLFetch != COMPLETED) {
        mPendingResults.clear();
        skipCurrent();
        return;
    }

    mMDResolveHandle = resolveMetaDataAssets(mPendingResults.front(), mCurrentSearch);
    mPendingResults.clear();
}

void ThreadedScraper::acceptResult(const ScraperSearchResult& result)
{
    mCurrentSearch.system->getIndex()->removeFromIndex(mCurrentSearch.game);

    GuiScraperSearch::saveMetadata(result, mCurrentSearch.game->metadata, mCurrentSearch.game);
    GamelistFileParser::updateGamelist(mCurrentSearch.system);

    mCurrentSearch.system->getIndex()->addToIndex(mCurrentSearch.game);

    ++mCurrentGame;
    ++mTotalSuccessful;

    CollectionSystemsManager::getInstance()->refreshCollectionSystems(mCurrentSearch.game);

    mSearchQueue.pop();
    mHasCurrentSearch = false;
    mPendingResults.clear();
    updateProgressPopup();
}

void ThreadedScraper::skipCurrent()
{
    ++mCurrentGame;
    ++mTotalSkipped;

    if (!mSearchQueue.empty())
        mSearchQueue.pop();

    mHasCurrentSearch = false;
    mPendingResults.clear();
    updateProgressPopup();
}

void ThreadedScraper::updateProgressPopup()
{
    const int done {mCurrentGame};
    const int total {std::max(1, mTotalGames)};
    const int percent {std::clamp((done * 100) / total, 0, 100)};

    std::stringstream ss;
    ss << _("SCRAPING IN BACKGROUND") << " (" << done << "/" << total << ")";
    Window::getInstance()->setPersistentInfoPopup(ss.str(), percent);
}

void ThreadedScraper::finish(bool stoppedByUser)
{
    Scripting::fireEvent("scraper-end", std::to_string(mTotalSuccessful), std::to_string(mTotalSkipped),
                         (stoppedByUser ? "stopped" : "finished"), "");

    Window::getInstance()->clearPersistentInfoPopup();

    if (mTotalSuccessful > 0 || mSavedNewMedia) {
        for (auto system : SystemData::sSystemVector)
            system->sortSystem();
    }

    std::stringstream ss;

    if (stoppedByUser) {
        ss << _("BACKGROUND SCRAPING STOPPED") << " (" << mTotalSuccessful << "/" << mTotalGames
           << ")";
    }
    else if (mTotalSuccessful == 0) {
        ss << _("NO GAMES WERE SCRAPED");
    }
    else {
        ss << Utils::String::format(
            _n("%i GAME SUCCESSFULLY SCRAPED", "%i GAMES SUCCESSFULLY SCRAPED", mTotalSuccessful),
            mTotalSuccessful);

        if (mTotalSkipped > 0)
            ss << " (" << mTotalSkipped << " "
               << _n("GAME SKIPPED", "GAMES SKIPPED", mTotalSkipped) << ")";
    }

    Window::getInstance()->queueInfoPopup(ss.str(), 4000);
}
