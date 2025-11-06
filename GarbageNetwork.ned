import ned.DatarateChannel;

// ---------- Reusable channels ----------
channel ClientLink extends DatarateChannel
{
    delay = default(parent.clientDelay);
    datarate = default(parent.clientDatarate);
}
channel SlowCloudLink extends DatarateChannel
{
    delay = default(parent.slowDelay);
    datarate = default(parent.slowDatarate);
}
channel FastFogLink extends DatarateChannel
{
    delay = default(parent.fastDelay);
    datarate = default(parent.fastDatarate);
}

// ---------- Simple modules ----------
simple Smartphone
{
    parameters:
        string mode = default("cloud");              // "cloud" | "fog" | "none"
        double checkInterval @unit(s) = default(0.5s);

        // GUI movement & proximity (unit-less; interpreted as pixels)
        double speed = default(60);                 // pixels per second
        double moveStep @unit(s) = default(0.05s);  // GUI update period
        double proximityRange = default(140);       // pixels
        double startAboveCloudDy = default(100);

    gates:
        input inFromCan1;
        output outToCan1;
        input inFromCan2;
        output outToCan2;
        input inFromCloud;
        output outToCloud;
}


simple Can
{
    parameters:
        int canId = default(1); // 1 or 2
        int dropCount = default(3); // lose first N checks
        bool cansHaveGarbage = default(true);
        string mode = default("cloud");
    gates:
        input inFromPhone;
        output outToPhone;
        input inFromCloud;
        output outToCloud;
}

simple Cloud
{
    gates:
        input inFromPhone;
        output outToPhone;
        input inFromCan1;
        output outToCan1;
        input inFromCan2;
        output outToCan2;
}

// ---------- Network ----------
network GarbageNetwork
{
    parameters:
        @figure[titleText](type=text; pos=700,30; text=""; font=Arial,12,bold);
        @figure[statusText](type=text; pos=700,80; text=""; font=Arial,12);

        @figure[road1](type=line; points=600,100,50,100);
        @figure[road2](type=line; points=50,100,50,400);
        @figure[road3](type=line; points=50,400,600,400);

		@figure[road4](type=line; points=600,200,200,200);
        @figure[road5](type=line; points=200,200,200,300);
        @figure[road6](type=line; points=200,300,600,300);

        string title;

        // scenario knobs
        bool cansHaveGarbage = default(true);
        int dropCount = default(3);
        double checkInterval @unit(s) = default(0.5s);

        // link characteristics
        double slowDelay @unit(s) = default(100ms);
        double slowDatarate @unit(bps) = default(5Mbps);

        double fastDelay @unit(s) = default(5ms);
        double fastDatarate @unit(bps) = default(100Mbps);

        double clientDelay @unit(s) = default(10ms);
        double clientDatarate @unit(bps) = default(100Mbps);

        double can1X = default(150);
        double can1Y = default(50);
        double can2X = default(100);
        double can2Y = default(450);
        double cloudX = default(600);
        double cloudY = default(250);

        @display("bgb=1110,500");
    submodules:
        smartphone: Smartphone {
            parameters:
                checkInterval = parent.checkInterval;
                // start where Smartphone.cc expects (x=xMin, y=y); we only set icon here
                @display("i=device/palm;p=600,205");
        }
        can1: Can {
            parameters:
                canId = 1;
                dropCount = parent.dropCount;
                cansHaveGarbage = parent.cansHaveGarbage;
                @display("p=100,50;i=block/circle");
        }
        can2: Can {
            parameters:
                canId = 2;
                dropCount = parent.dropCount;
                cansHaveGarbage = parent.cansHaveGarbage;
                @display("p=100,450;i=block/circle");
        }
        cloud: Cloud {
            @display("p=600,250;i=misc/cloud");
        }

    connections allowunconnected:
        // Smartphone <-> Cans (client links)
        smartphone.outToCan1 --> ClientLink --> can1.inFromPhone;
        can1.outToPhone --> ClientLink --> smartphone.inFromCan1;

        smartphone.outToCan2 --> ClientLink --> can2.inFromPhone;
        can2.outToPhone --> ClientLink --> smartphone.inFromCan2;

        // Smartphone <-> Cloud (slow link)
        smartphone.outToCloud --> SlowCloudLink --> cloud.inFromPhone;
        cloud.outToPhone --> SlowCloudLink --> smartphone.inFromCloud;

        // Cans <-> Cloud (fast links)
        can1.outToCloud --> FastFogLink --> cloud.inFromCan1;
        cloud.outToCan1 --> FastFogLink --> can1.inFromCloud;

        can2.outToCloud --> FastFogLink --> cloud.inFromCan2;
        cloud.outToCan2 --> FastFogLink --> can2.inFromCloud;
}
