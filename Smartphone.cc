/*
#include <omnetpp.h>
#include <cmath>
#include <string>

using namespace omnetpp;

class Smartphone : public cSimpleModule
{
  private:
    // --- Motion ---
    struct Pt { double x, y; };
    std::vector<Pt> waypoints;
    int wpIdx = 0;
    double x = 0, y = 0;
    double speed = 60;                 // pixels per second
    simtime_t moveStep = 0.05;         // GUI update period
    double epsilon = 1.0;              // waypoint snap threshold (pixels)
    cMessage *moveTimer = nullptr;

    // --- Proximity pinging ---
    simtime_t checkInterval;
    simtime_t nextCheck1 = SIMTIME_ZERO;
    simtime_t nextCheck2 = SIMTIME_ZERO;
    double range = 120;

    // --- Reply state ---
    bool gotReply1 = false, gotReply2 = false;
    bool yes1 = false, yes2 = false;
    bool ok8 = false, ok10 = false;

    // --- Mode ---
    enum Mode { CLOUD, FOG, NONE } mode = CLOUD;

    // --- Landmarks (from network params) ---
    double can1X = 100, can1Y = 50;
    double can2X = 100, can2Y = 350;
    double cloudX = 800, cloudY = 200;
    double startAboveCloudDy = 30;

  private:
    Mode parseMode(const char* s) {
        std::string m = s ? std::string(s) : "cloud";
        for (auto &ch : m) ch = (char)tolower(ch);
        if (m == "cloud") return CLOUD;
        if (m == "fog")   return FOG;
        return NONE;
    }
    void setGuiPos(double nx, double ny) {
        cDisplayString& ds = getDisplayString();
        ds.setTagArg("p", 0, (long)std::round(nx));
        ds.setTagArg("p", 1, (long)std::round(ny));
    }
    double dist(double ax, double ay, double bx, double by) const {
        double dx = ax - bx, dy = ay - by;
        return std::sqrt(dx*dx + dy*dy);
    }
    void sendCheck(int canIdx) {
        if (canIdx == 1) {
            auto *msg = new cMessage("1-Is the can full?");
            msg->setKind(1);
            send(msg, "outToCan1");
            EV_INFO << "Phone -> Can1: 1-Is the can full?\n";
        } else {
            auto *msg = new cMessage("4-Is the can full?");
            msg->setKind(4);
            send(msg, "outToCan2");
            EV_INFO << "Phone -> Can2: 4-Is the can full?\n";
        }
    }
    void maybeSendCollectToCloud() {
        if (mode != CLOUD) return;
        if (!(yes1 && yes2)) return;
        auto *m7 = new cMessage("7-Collect garbage"); m7->setKind(7); send(m7, "outToCloud");
        auto *m9 = new cMessage("9-Collect garbage"); m9->setKind(9); send(m9, "outToCloud");
        EV_INFO << "Phone -> Cloud: 7/9-Collect garbage\n";
    }
    void stepMotion() {
        // Move toward current waypoint
        if (wpIdx < (int)waypoints.size()) {
            Pt t = waypoints[wpIdx];
            double dx = t.x - x, dy = t.y - y;
            double d = std::sqrt(dx*dx + dy*dy);
            double step = speed * moveStep.dbl();
            if (d <= std::max(epsilon, step)) {
                x = t.x; y = t.y;
                wpIdx++;
            } else {
                x += (dx/d) * step;
                y += (dy/d) * step;
            }
            setGuiPos(x, y);
        }

        // Proximity-based checks
        if (!gotReply1 && dist(x,y, can1X,can1Y) <= range && simTime() >= nextCheck1) {
            sendCheck(1);
            nextCheck1 = simTime() + checkInterval;
        }
        if (!gotReply2 && dist(x,y, can2X,can2Y) <= range && simTime() >= nextCheck2) {
            sendCheck(2);
            nextCheck2 = simTime() + checkInterval;
        }

        // Keep moving until final waypoint reached
        if (wpIdx < (int)waypoints.size())
            scheduleAt(simTime() + moveStep, moveTimer);
        else
            EV_INFO << "Phone reached final waypoint near cloud; stopping motion.\n";
    }

  protected:
    virtual void initialize() override {
        mode = parseMode(par("mode").stringValue());
        checkInterval = par("checkInterval");
        speed         = par("speed").doubleValue();
        moveStep      = par("moveStep");
        range         = par("proximityRange").doubleValue();
        startAboveCloudDy = par("startAboveCloudDy").doubleValue();

        // Query network coordinates
        cModule *net = getParentModule();
        can1X = net->par("can1X").doubleValue();
        can1Y = net->par("can1Y").doubleValue();
        can2X = net->par("can2X").doubleValue();
        can2Y = net->par("can2Y").doubleValue();
        cloudX = net->par("cloudX").doubleValue();
        cloudY = net->par("cloudY").doubleValue();

        // Build the path:
        // start a bit above cloud -> left to can1 (same y as start) -> down to can2 -> right to cloud
        Pt start   { cloudX, cloudY - startAboveCloudDy };
        Pt toCan1H { can1X,  start.y };
        Pt toCan2V { can1X,  cloudY + startAboveCloudDy };
        Pt toCloud { cloudX, cloudY + startAboveCloudDy };
        waypoints = { start, toCan1H, toCan2V, toCloud };

        // Initialize position to the first waypoint and show it
        x = waypoints.front().x; y = waypoints.front().y;
        setGuiPos(x, y);

        moveTimer = new cMessage("moveTimer");
        scheduleAt(simTime(), moveTimer);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == moveTimer) { stepMotion(); return; }

        int k = msg->getKind();
        const char* gateName = msg->getArrivalGate()->getName();

        if (!strcmp(gateName, "inFromCan1")) {
            if (k == 2 || k == 3) {
                gotReply1 = true; if (k == 3) yes1 = true;
                EV_INFO << "Phone <- Can1: " << msg->getName() << "\n";
                maybeSendCollectToCloud();
            }
            delete msg; return;
        }
        if (!strcmp(gateName, "inFromCan2")) {
            if (k == 5 || k == 6) {
                gotReply2 = true; if (k == 6) yes2 = true;
                EV_INFO << "Phone <- Can2: " << msg->getName() << "\n";
                maybeSendCollectToCloud();
            }
            delete msg; return;
        }
        if (!strcmp(gateName, "inFromCloud")) {
            if (k == 8)  { ok8  = true; EV_INFO << "Phone <- Cloud: 8-OK\n"; }
            if (k == 10) { ok10 = true; EV_INFO << "Phone <- Cloud: 10-OK\n"; }
            delete msg; return;
        }
        delete msg;
    }

    virtual void finish() override {
        if (moveTimer) { cancelEvent(moveTimer); delete moveTimer; moveTimer = nullptr; }
    }
};

Define_Module(Smartphone);
*/

#include <omnetpp.h>
#include <cmath>
#include <string>
#include <vector>

using namespace omnetpp;

class Smartphone : public cSimpleModule
{
  private:
    // --- Motion / path ---
    struct Pt { double x, y; };
    std::vector<Pt> waypoints;        // [ startAboveCloud, leftToCan1, downToCan2, rightToCloud ]
    int wpIdx = 0;                    // index of current target waypoint
    double x = 0, y = 0;
    double speed = 60;                // pixels per second
    simtime_t moveStep = 0.05;        // GUI update period
    double epsilon = 1.0;             // waypoint snap threshold (pixels)
    cMessage *tick = nullptr;         // timer for both motion + periodic checks

    // --- Proximity / retrying ---
    simtime_t checkInterval;          // resend cadence while at a can
    simtime_t nextCheck1 = SIMTIME_ZERO;
    simtime_t nextCheck2 = SIMTIME_ZERO;
    double range = 120;               // treat as "in range" radius for sending 1/4

    // --- Reply state ---
    bool gotReply1 = false, gotReply2 = false;
    bool yes1 = false, yes2 = false;

    // --- Cloud acks (only relevant in CLOUD mode) ---
    bool sent7 = false, sent9 = false;
    bool ok8 = false, ok10 = false;

    // --- Mode ---
    enum Mode { CLOUD, FOG, NONE } mode = CLOUD;

    // --- Landmarks (from network params) ---
    double can1X = 100, can1Y = 50;
    double can2X = 100, can2Y = 350;
    double cloudX = 800, cloudY = 200;
    double startAboveCloudDy = 30;

    // --- Phase machine ---
    enum Phase {
        TO_CAN1,        // move to can1
        AT_CAN1,        // hold position until can1 conversation finished (and cloud ack if needed)
        TO_CAN2,        // move to can2
        AT_CAN2,        // hold until can2 conversation finished (and cloud ack if needed)
        TO_CLOUD,       // move back to cloud
        DONE
    } phase = TO_CAN1;

  private:
    Mode parseMode(const char* s) {
        std::string m = s ? std::string(s) : "cloud";
        for (auto &ch : m) ch = (char)tolower(ch);
        if (m == "cloud") return CLOUD;
        if (m == "fog")   return FOG;
        return NONE;
    }

    void setGuiPos(double nx, double ny) {
        cDisplayString& ds = getDisplayString();
        ds.setTagArg("p", 0, (long)std::lround(nx));
        ds.setTagArg("p", 1, (long)std::lround(ny));
    }

    static double distance(double ax, double ay, double bx, double by) {
        const double dx = ax - bx, dy = ay - by;
        return std::sqrt(dx*dx + dy*dy);
    }

    void sendCheck(int canIdx) {
        if (canIdx == 1) {
            auto *msg = new cMessage("1-Is the can full?");
            msg->setKind(1);
            send(msg, "outToCan1");
            EV_INFO << "Phone -> Can1: 1-Is the can full?\n";
        } else {
            auto *msg = new cMessage("4-Is the can full?");
            msg->setKind(4);
            send(msg, "outToCan2");
            EV_INFO << "Phone -> Can2: 4-Is the can full?\n";
        }
    }

    void maybeSendCollectAfterCan(int canIdx) {
        if (mode != CLOUD) return;          // only in cloud mode the phone notifies cloud
        if (canIdx == 1) {
            if (yes1 && !sent7) {
                auto *m7 = new cMessage("7-Collect garbage"); m7->setKind(7);
                send(m7, "outToCloud");
                sent7 = true;
                EV_INFO << "Phone -> Cloud: 7-Collect garbage (for Can1)\n";
            }
        } else {
            if (yes2 && !sent9) {
                auto *m9 = new cMessage("9-Collect garbage"); m9->setKind(9);
                send(m9, "outToCloud");
                sent9 = true;
                EV_INFO << "Phone -> Cloud: 9-Collect garbage (for Can2)\n";
            }
        }
    }

    // Move toward current waypoint ONLY in moving phases
    void maybeMove() {
        if (phase != TO_CAN1 && phase != TO_CAN2 && phase != TO_CLOUD)
            return; // hold position during AT_* phases and after DONE

        if (wpIdx >= (int)waypoints.size())
            return;

        Pt t = waypoints[wpIdx];
        double dx = t.x - x, dy = t.y - y;
        double d = std::sqrt(dx*dx + dy*dy);
        double step = speed * moveStep.dbl();

        if (d <= std::max(epsilon, step)) {
            x = t.x; y = t.y;
            setGuiPos(x, y);
            // reached waypoint: advance phase
            if (phase == TO_CAN1)        { phase = AT_CAN1;   EV_INFO << "Arrived at Can1; holding.\n"; }
            else if (phase == TO_CAN2)   { phase = AT_CAN2;   EV_INFO << "Arrived at Can2; holding.\n"; }
            else if (phase == TO_CLOUD)  { phase = DONE;      EV_INFO << "Arrived at Cloud; finished.\n"; }
            wpIdx = std::min((int)waypoints.size(), wpIdx + 1);
        } else {
            x += (dx/d) * step;
            y += (dy/d) * step;
            setGuiPos(x, y);
        }
    }

    // While holding at a can, handle ping/retry and escalate to cloud, then release to next leg
    void processAtCan() {
        if (phase == AT_CAN1) {
            // Send/Retry 1- if needed
            if (!gotReply1 && simTime() >= nextCheck1 &&
                distance(x,y, can1X,can1Y) <= range) {
                sendCheck(1);
                nextCheck1 = simTime() + checkInterval;
            }

            // If reply arrived, in CLOUD mode send 7 and wait for 8
            if (gotReply1) {
                if (mode == CLOUD && yes1) {
                    maybeSendCollectAfterCan(1);
                    if (ok8) { phase = TO_CAN2; EV_INFO << "Got 8-OK; proceeding to Can2.\n"; }
                } else {
                    phase = TO_CAN2; // either NO or not cloud mode → proceed
                }
            }
        }
        else if (phase == AT_CAN2) {
            if (!gotReply2 && simTime() >= nextCheck2 &&
                distance(x,y, can2X,can2Y) <= range) {
                sendCheck(2);
                nextCheck2 = simTime() + checkInterval;
            }

            if (gotReply2) {
                if (mode == CLOUD && yes2) {
                    maybeSendCollectAfterCan(2);
                    if (ok10) { phase = TO_CLOUD; EV_INFO << "Got 10-OK; returning to Cloud.\n"; }
                } else {
                    phase = TO_CLOUD;
                }
            }
        }
    }

  protected:
    virtual void initialize() override {
        mode = parseMode(par("mode").stringValue());
        checkInterval     = par("checkInterval");
        speed             = par("speed").doubleValue();
        moveStep          = par("moveStep");
        range             = par("proximityRange").doubleValue();
        startAboveCloudDy = par("startAboveCloudDy").doubleValue();

        // Get landmarks from network
        cModule *net = getParentModule();
        can1X  = net->par("can1X").doubleValue();
        can1Y  = net->par("can1Y").doubleValue();
        can2X  = net->par("can2X").doubleValue();
        can2Y  = net->par("can2Y").doubleValue();
        cloudX = net->par("cloudX").doubleValue();
        cloudY = net->par("cloudY").doubleValue();

        // Define strict straight-line path: left -> down -> right
        Pt start   { cloudX,             cloudY - startAboveCloudDy };
        Pt toCan1H { can1X,              cloudY - startAboveCloudDy };
        Pt toCan2V { can1X,              cloudY + startAboveCloudDy };
        Pt toCloud { cloudX,             cloudY + startAboveCloudDy };
        waypoints = { start, toCan1H, toCan2V, toCloud };

        // Start at first waypoint (above cloud)
        x = waypoints.front().x; y = waypoints.front().y;
        setGuiPos(x, y);

        phase = TO_CAN1;
        wpIdx = 1; // current target is waypoints[1] (toCan1H)

        tick = new cMessage("phoneTick");
        scheduleAt(simTime(), tick);
    }

    virtual void handleMessage(cMessage *msg) override {
        if (msg == tick) {
            // motion only in moving phases; hold otherwise
            maybeMove();
            // handle can logic while holding
            processAtCan();
            // continue ticking until DONE
            if (phase != DONE) scheduleAt(simTime() + moveStep, tick);
            return;
        }

        // Handle network messages
        int k = msg->getKind();
        const char* gateName = msg->getArrivalGate()->getName();

        if (!strcmp(gateName, "inFromCan1")) {
            if (k == 2 || k == 3) {
                gotReply1 = true; yes1 = (k == 3);
                EV_INFO << "Phone <- Can1: " << msg->getName() << "\n";
            }
            delete msg; return;
        }
        if (!strcmp(gateName, "inFromCan2")) {
            if (k == 5 || k == 6) {
                gotReply2 = true; yes2 = (k == 6);
                EV_INFO << "Phone <- Can2: " << msg->getName() << "\n";
            }
            delete msg; return;
        }
        if (!strcmp(gateName, "inFromCloud")) {
            if (k == 8)  { ok8  = true; EV_INFO << "Phone <- Cloud: 8-OK\n"; }
            if (k == 10) { ok10 = true; EV_INFO << "Phone <- Cloud: 10-OK\n"; }
            delete msg; return;
        }
        delete msg;
    }

    virtual void finish() override {
        if (tick) { cancelEvent(tick); delete tick; tick = nullptr; }
    }
};

Define_Module(Smartphone);
