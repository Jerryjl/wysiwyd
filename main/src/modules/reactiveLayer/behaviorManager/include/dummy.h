#include <iostream>
#include <yarp/os/all.h>
#include "wrdac/clients/icubClient.h"
#include "behavior.h"

class Dummy: public Behavior
{
private:
    static int n_instances;

    void run(const yarp::os::Bottle &args) {
        yDebug() << "Dummmy::run start " + behaviorName;
        yarp::os::Time::delay(4);
        yDebug() << "Dummmy::run stop " + behaviorName;
    }
    int id;

public:
    Dummy(yarp::os::Mutex* mut, yarp::os::ResourceFinder &rf, std::string behaviorName): Behavior(mut, rf, behaviorName) {
        n_instances++;
        id = n_instances;
    }

    void configure() {
        external_port_name = "None";
        from_sensation_port_name = "None";        
    }


    void close_extra_ports() {
        ;
    }

};

