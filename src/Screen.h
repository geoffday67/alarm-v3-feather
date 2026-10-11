#pragma once

class Screen {
protected:
    Screen ();
    virtual ~Screen();

    bool active;
    virtual void onActivate();
    virtual void onDeactivate();

public:
    virtual void activate();
    virtual void deactivate();
};
