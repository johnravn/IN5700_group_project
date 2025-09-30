#include <omnetpp.h>

using namespace omnetpp;

class Cloud : public cSimpleModule
{
  protected:
    virtual void handleMessage(cMessage *msg) override {
        int k = msg->getKind();
        cGate *inGate = msg->getArrivalGate();

        // Map Collect -> OK (7->8, 9->10)
        cMessage *ack = nullptr;
        if (k == 7) { ack = new cMessage("8-OK");  ack->setKind(8); }
        else if (k == 9) { ack = new cMessage("10-OK"); ack->setKind(10); }

        if (ack != nullptr) {
            // reply back to whoever sent it
            const char* inName = inGate->getName();
            if (!strcmp(inName, "inFromPhone")) {
                send(ack, "outToPhone");
            } else if (!strcmp(inName, "inFromCan1")) {
                send(ack, "outToCan1");
            } else if (!strcmp(inName, "inFromCan2")) {
                send(ack, "outToCan2");
            } else {
                delete ack; // unknown
            }
        }
        delete msg;
    }
};

Define_Module(Cloud);
