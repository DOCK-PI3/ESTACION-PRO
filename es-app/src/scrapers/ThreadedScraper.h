//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  ThreadedScraper.h
//
//  Non-interactive multi-scraper running in the background while users keep navigating.
//

#ifndef ES_APP_SCRAPERS_THREADED_SCRAPER_H
#define ES_APP_SCRAPERS_THREADED_SCRAPER_H

#include "scrapers/Scraper.h"

#include <map>
#include <memory>
#include <queue>

class SystemData;

class ThreadedScraper
{
public:
    static void start(
        const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches);
    static void stop();
    static bool isRunning();
    static void update();

private:
    ThreadedScraper(
        const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches);

    void doNextSearch();
    void processSearchDone(const std::vector<ScraperSearchResult>& results);
    void processSearchError(const std::string& errorText);
    void processMediaURLsDone(const std::vector<ScraperSearchResult>& mediaResults);
    void acceptResult(const ScraperSearchResult& result);
    void skipCurrent();
    void updateProgressPopup();
    void finish(bool stoppedByUser);

    static std::unique_ptr<ThreadedScraper> sInstance;

    std::queue<ScraperSearchParams> mSearchQueue;
    std::map<SystemData*, int> mQueueCountPerSystem;

    std::unique_ptr<ScraperSearchHandle> mSearchHandle;
    std::unique_ptr<ScraperSearchHandle> mMDRetrieveURLsHandle;
    std::unique_ptr<MDResolveHandle> mMDResolveHandle;
    std::vector<ScraperSearchResult> mPendingResults;

    ScraperSearchParams mCurrentSearch;
    bool mHasCurrentSearch;

    int mTotalGames;
    int mCurrentGame;
    int mTotalSuccessful;
    int mTotalSkipped;

    bool mSavedNewMedia;
};

#endif // ES_APP_SCRAPERS_THREADED_SCRAPER_H
