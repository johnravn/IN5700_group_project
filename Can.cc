#include <omnetpp.h>
#include <string>

using namespace omnetpp;

class Can : public cSimpleModule
{
  private:
    int canId = 1;                // 1 or 2
    int dropsRemaining = 3;       // lose first N "Is the can full?" messages
    bool hasGarbage = true;

    enum Mode { CLOUD, FOG, NONE } mode = CLOUD;

    Mode parseMode(const char* s) {
        std::string m = s ? std::string(s) : "cloud";
        for (auto &ch : m) ch = (char)tolower(ch);
        if (m == "cloud") return CLOUD;
        if (m == "fog")   return FOG;
        return NONE;
    }

    // Map helpers
    int expectedCheckKind() const { return canId==1 ? 1 : 4; }
    int noKind() const           { return canId==1 ? 2 : 5; }
    int yesKind() const          { return canId==1 ? 3 : 6; }
    int collectKind() const      { return canId==1 ? 7 : 9; }
    int okKind() const           { return canId==1 ? 8 : 10; }
    const char* collectName() const { return canId==1 ? "7-Collect garbage" : "9-Collect garbage"; }
    const char* okName() const      { return canId==1 ? "8-OK" : "10-OK"; }

    void replyToPhone(bool yes) {
        cMessage *reply = nullptr;
        if (yes) {
            reply = new cMessage(canId==1 ? "3-YES" : "6-YES");
            reply->setKind(yesKind());
        } else {
            reply = new cMessage(canId==1 ? "2-NO" : "5-NO");
            reply->setKind(noKind());
        }
        send(reply, "outToPhone");
    }

    void maybeFogCollect() {
        if (mode != FOG) return;
        if (!hasGarbage) return;

        auto *m = new cMessage(collectName());
        m->setKind(collectKind());
        send(m, "outToCloud");
    }

  protected:
    virtual void initialize() override {
        canId = par("canId");
        dropsRemaining = par("dropCount");
        hasGarbage = par("cansHaveGarbage").boolValue();
        mode = parseMode(par("mode").stringValue());
    }

    virtual void handleMessage(cMessage *msg) override {
        int k = msg->getKind();
        const char* gateName = msg->getArrivalGate()->getName();

        if (!strcmp(gateName, "inFromPhone")) {
            // Expect "1-Is the can full?" for can1 or "4-..." for can2
            if (k == expectedCheckKind()) {
                if (dropsRemaining > 0) {
                    bubble("Lost message!");
                    dropsRemaining--;
                    EV_WARN << "Can" << canId << " dropping '" << msg->getName()
                            << "'. Drops remaining: " << dropsRemaining << "\n";
                    delete msg; // simulate loss
                    return;
                }
                // Now reply
                EV_INFO << "Can" << canId << " received check; replying "
                        << (hasGarbage ? "YES" : "NO") << "\n";
                delete msg;
                replyToPhone(hasGarbage);
                // In fog mode, cans also notify cloud when they have garbage
                maybeFogCollect();
                return;
            }
            // Unexpected
            delete msg;
            return;
        }

        if (!strcmp(gateName, "inFromCloud")) {
            // Expect "8-OK" for 7, or "10-OK" for 9
            if (k == okKind()) {
                EV_INFO << "Can" << canId << " received " << okName() << " from cloud\n";
            }
            delete msg;
            return;
        }

        delete msg;
    }
};

Define_Module(Can);
