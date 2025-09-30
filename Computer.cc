/*
 * Computer.cc
 *
 *  Created on: 26. sep. 2025
 *      Author: johnravndal
 */

#include <omnetpp.h>

using namespace omnetpp;

enum MsgKind {
    K_HELLO = 1,
    K_ACK   = 2,
    K_TEST  = 3,
    K_TIMEOUT = 100,
    K_START = 101
};

class Computer : public cSimpleModule
{
  private:
    // State
    bool waitingForHelloAck = false;
    bool waitingForTestAck  = false;

    // Retry logic for the test message
    int testAttempts = 0;         // how many times we've sent message 3
    int maxTestAttempts = 4;      // MUST succeed on 4th
    simtime_t ackTimeout = 1.0;   // seconds to wait before retrying

    // Self-messages
    cMessage *startEvt = nullptr;
    cMessage *timeoutEvt = nullptr;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    void sendHello();
    void sendTestMessage();
};

Define_Module(Computer);

void Computer::initialize()
{
    // You can also fetch these from omnetpp.ini with par("...") if you add params.
    ackTimeout = 1.0;

    startEvt = new cMessage("start", K_START);
    timeoutEvt = new cMessage("ack-timeout", K_TIMEOUT);

    // Kick off the sequence
    scheduleAt(simTime(), startEvt);
}

void Computer::sendHello()
{
    EV_INFO << "[Computer] Sending 1 - Hello\n";
    auto *m = new cMessage("1-Hello", K_HELLO);
    send(m, "out");
    waitingForHelloAck = true;
}

void Computer::sendTestMessage()
{
    testAttempts++;
    EV_INFO << "[Computer] Sending 3 - Test message (attempt " << testAttempts << " of " << maxTestAttempts << ")\n";
    auto *m = new cMessage("3-Test message", K_TEST);
    send(m, "out");
    waitingForTestAck = true;

    // (Re)schedule an ACK timeout for this attempt
    if (!timeoutEvt->isScheduled())
        scheduleAt(simTime() + ackTimeout, timeoutEvt);
    else
        rescheduleAt(simTime() + ackTimeout, timeoutEvt);
}

void Computer::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        if (msg->getKind() == K_START) {
            sendHello();
        } else if (msg->getKind() == K_TIMEOUT) {
            // No ACK arrived in time for the current phase
            if (waitingForHelloAck) {
                EV_WARN << "[Computer] Timeout waiting for ACK to Hello. (Should not happen if Cloud is correct.) Retrying Hello.\n";
                sendHello();
            } else if (waitingForTestAck) {
                if (testAttempts < maxTestAttempts) {
                    EV_WARN << "[Computer] Timeout waiting for ACK to Test message. Retrying...\n";
                    sendTestMessage();
                } else {
                    EV_ERROR << "[Computer] Gave up after " << testAttempts << " attempts (expected success on 4th).\n";
                    // stop expecting ACK
                    waitingForTestAck = false;
                }
            }
        }
        return;
    }

    // Incoming from network
    if (msg->getKind() == K_ACK) {
        // Decide which ACK we were waiting for based on the current phase
        if (waitingForHelloAck) {
            EV_INFO << "[Computer] Received 2 - ACK for Hello\n";
            waitingForHelloAck = false;

            // Proceed to test message attempts
            // Clear any running timeout (defensive)
            if (timeoutEvt->isScheduled())
                cancelEvent(timeoutEvt);

            // Start attempts
            testAttempts = 0;
            sendTestMessage();
        }
        else if (waitingForTestAck) {
            EV_INFO << "[Computer] Received 4 - ACK for Test message (success on attempt " << testAttempts << ")\n";
            waitingForTestAck = false;

            // Stop timeout timer
            if (timeoutEvt->isScheduled())
                cancelEvent(timeoutEvt);
        }
        else {
            EV_WARN << "[Computer] Unexpected ACK (no ACK expected at the moment)\n";
        }
    } else {
        EV_WARN << "[Computer] Unexpected message kind=" << msg->getKind()
                << " name=" << msg->getName() << "\n";
    }

    delete msg;
}

void Computer::finish()
{
    cancelAndDelete(startEvt);
    cancelAndDelete(timeoutEvt);
}
