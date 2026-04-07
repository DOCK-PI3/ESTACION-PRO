//  SPDX-License-Identifier: MIT
//
//  ESTACION-PRO Frontend
//  GuiInfoPopup.h
//
//  Popup window used for user notifications.
//

#ifndef ES_APP_GUIS_GUI_INFO_POPUP_H
#define ES_APP_GUIS_GUI_INFO_POPUP_H

#include "GuiComponent.h"
#include "components/BackgroundComponent.h"
#include "components/TextComponent.h"
#include "renderers/Renderer.h"

class ComponentGrid;
class BackgroundComponent;

class GuiInfoPopup : public GuiComponent
{
public:
    GuiInfoPopup(std::string message, int duration, bool persistent = false, int progress = -1);
    ~GuiInfoPopup();

    void render(const glm::mat4& parentTrans);
    void stop() { mRunning = false; }
    bool isRunning() { return mRunning; }
    void setMessage(const std::string& message);
    void setProgress(int progress) { mProgress = progress; }
    void setPersistent(bool persistent) { mPersistent = persistent; }

private:
    bool updateState();

    Renderer* mRenderer;
    ComponentGrid* mGrid;
    BackgroundComponent* mBackground;
    std::shared_ptr<TextComponent> mText;

    std::string mMessage;
    int mDuration;
    float mAlpha;
    int mStartTime;
    bool mRunning;
    bool mPersistent;
    int mProgress;
};

#endif // ES_APP_GUIS_GUI_INFO_POPUP_H
