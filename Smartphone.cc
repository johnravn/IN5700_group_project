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
    double speed = 160;                // pixels per second
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
    void updateStatusText(const char* text) {
        // Access the network's canvas (parent module)
        cModule *network = getParentModule();
        cCanvas *canvas = network->getCanvas();
        if (canvas) {
            cFigure *figure = canvas->getFigure("statusText");

            if (figure) {
                cTextFigure *textFigure = check_and_cast<cTextFigure *>(figure);
                textFigure->setText(text);
            }

            cFigure *titleFigure = canvas->getFigure("titleText");
            if (titleFigure) {
                cTextFigure  *titleTextFigure = check_and_cast<cTextFigure *>(titleFigure);

                const char *title = network->par("title").stringValue();

                titleTextFigure->setText(title);
            }
        }

    }

  protected:
    virtual void initialize() override {
        // Build statusText
        std::ostringstream oss;

        oss << "Slow connection from the smartphone to others (time it takes) = 0\n";
        oss << "Slow connection from others to the smartphone (time it takes) = 0\n";
        oss << "Fast connection from the smartphone to others (time it takes) = 0\n";
        oss << "Fast connection from others to the smartphone (time it takes) = 0\n\n";

        oss << "Connection from Can1 to others (time it takes) = 0\n";
        oss << "Connection from others to Can1 (time it takes) = 0\n\n";

        oss << "Connection from Can2 to others (time it takes) = 0\n";
        oss << "Connection from others to Can2 (time it takes) = 0\n\n";

        oss << "Slow connection from the cloud to others (time it takes) = 0\n";
        oss << "Slow connection from others to the cloud (time it takes) = 0\n";
        oss << "Fast connection from the cloud to others (time it takes) = 0\n";
        oss << "Fast connection from others to the cloud (time it takes) = 0";

        updateStatusText(oss.str().c_str());

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
        Pt start { cloudX, cloudY - startAboveCloudDy };
        Pt toCan1H { can1X, cloudY - startAboveCloudDy };
        Pt toCan2V { can1X, cloudY + 100 + startAboveCloudDy };
        Pt toCloud { cloudX, cloudY + 100 + startAboveCloudDy };
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

        mode = parseMode(par("mode").stringValue());

        cModule *net = getParentModule();

        double clientDelay = net->par("clientDelay").doubleValue() * 1000;
        double fastDelay = net->par("fastDelay").doubleValue() * 1000;
        double slowDelay = net->par("slowDelay").doubleValue() * 1000;

        int slowSmartphoneToOthers;
        int slowOthersToSmartphone;
        int fastSmartphoneToOthers;
        int fastOthersToSmartphone;

        int cansToOthers;
        int othersToCans;
        
        int slowCloudToOthers;
        int slowOthersToCloud;
        int fastCloudToOthers;
        int fastOthersToCloud;

        if (mode == CLOUD) {
            slowSmartphoneToOthers = 2 * slowDelay;
            slowOthersToSmartphone = 2 * slowDelay;
            fastSmartphoneToOthers = 8 * clientDelay;
            fastOthersToSmartphone = 2 * clientDelay;

            cansToOthers = 1 * clientDelay;
            othersToCans = 1 * clientDelay;

            slowCloudToOthers = 2 * slowDelay;
            slowOthersToCloud = 2 * slowDelay;
            fastCloudToOthers = 0;
            fastOthersToCloud = 0;
        } else if (mode == FOG) {
            slowSmartphoneToOthers = 0;
            slowOthersToSmartphone = 0;
            fastSmartphoneToOthers = 8 * clientDelay;
            fastOthersToSmartphone = 2 * clientDelay;

            cansToOthers = 1 * fastDelay + 1 * clientDelay;
            othersToCans = 1 * fastDelay + 1 * clientDelay;

            slowCloudToOthers = 0;
            slowOthersToCloud = 0;
            fastCloudToOthers = 2 * fastDelay;
            fastOthersToCloud = 2 * fastDelay;
        } else if (mode == NONE) {
            slowSmartphoneToOthers = 0;
            slowOthersToSmartphone = 0;
            fastSmartphoneToOthers = 8 * clientDelay;
            fastOthersToSmartphone = 2 * clientDelay;

            cansToOthers = 1 * clientDelay;
            othersToCans = 1 * clientDelay;

            slowCloudToOthers = 0;
            slowOthersToCloud = 0;
            fastCloudToOthers = 0;
            fastOthersToCloud = 0;
        }

        // Build statusText
        std::ostringstream oss;

        oss << "Slow connection from the smartphone to others (time it takes) = " << std::to_string(slowSmartphoneToOthers) << "\n";
        oss << "Slow connection from others to the smartphone (time it takes) = " << std::to_string(slowOthersToSmartphone) << "\n";
        oss << "Fast connection from the smartphone to others (time it takes) = " << std::to_string(fastSmartphoneToOthers) << "\n";
        oss << "Fast connection from others to the smartphone (time it takes) = " << std::to_string(fastOthersToSmartphone) << "\n\n";

        oss << "Connection from Can1 to others (time it takes) = " << std::to_string(cansToOthers) << "\n";
        oss << "Connection from others to Can1 (time it takes) = " << std::to_string(othersToCans) << "\n\n";

        oss << "Connection from Can2 to others (time it takes) = " << std::to_string(cansToOthers) << "\n";
        oss << "Connection from others to Can2 (time it takes) = " << std::to_string(othersToCans) << "\n\n";

        oss << "Slow connection from the cloud to others (time it takes) = " << std::to_string(slowCloudToOthers) << "\n";
        oss << "Slow connection from others to the cloud (time it takes) = " << std::to_string(slowOthersToCloud) << "\n";
        oss << "Fast connection from the cloud to others (time it takes) = " << std::to_string(fastCloudToOthers) << "\n";
        oss << "Fast connection from others to the cloud (time it takes) = " << std::to_string(fastOthersToCloud);

        updateStatusText(oss.str().c_str());
    }
};

Define_Module(Smartphone);
