/*
 * Cloud.cc
 *
 *  Created on: 26. sep. 2025
 *      Author: johnravndal
 */

#include <omnetpp.h>

using namespace omnetpp;

enum MsgKind {
    K_HELLO = 1,
    K_ACK   = 2,
    K_TEST  = 3
};

class Cloud : public cSimpleModule
{
  private:
    int testDropsSoFar = 0;   // drop exactly the first 3 "Test message" receptions

  protected:
    virtual void handleMessage(cMessage *msg) override;
    void sendAck(const char *whatFor);
};

Define_Module(Cloud);

void Cloud::sendAck(const char *whatFor)
{
    auto *ack = new cMessage("ACK", K_ACK);
    // Optionally, you can add a display string or parameter here
    EV_INFO << "[Cloud] Sending ACK for " << whatFor << "\n";
    send(ack, "out");
}

void Cloud::handleMessage(cMessage *msg)
{
    if (msg->getKind() == K_HELLO) {
        EV_INFO << "[Cloud] Received 1 - Hello\n";
        sendAck("Hello");
    }
    else if (msg->getKind() == K_TEST) {
        EV_INFO << "[Cloud] Received 3 - Test message\n";
        if (testDropsSoFar < 3) {
            bubble("Message lost");
            testDropsSoFar++;
            EV_WARN << "[Cloud] Intentionally dropping Test message (drop "
                    << testDropsSoFar << " of 3)\n";
            // simulate loss: DO NOT send ACK
            // just delete the message
        } else {
            EV_INFO << "[Cloud] Accepting Test message (fourth attempt); replying with ACK\n";
            sendAck("Test message");
        }
    }
    else {
        EV_WARN << "[Cloud] Unexpected message kind=" << msg->getKind()
                << " name=" << msg->getName() << "\n";
    }

    delete msg;
}
