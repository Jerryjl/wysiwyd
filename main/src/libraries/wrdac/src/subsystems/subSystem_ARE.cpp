#include <yarp/os/all.h>
#include "wrdac/subsystems/subSystem_ARE.h"

void wysiwyd::wrdac::SubSystem_ARE::appendCartesianTarget(yarp::os::Bottle &b, const yarp::sig::Vector &t)
{
    yarp::os::Bottle &sub=b.addList();
    sub.addString("cartesian");
    for (size_t i=0; i<t.length(); i++)
        sub.addDouble(t[i]);
}

void wysiwyd::wrdac::SubSystem_ARE::selectHandCorrectTarget(yarp::os::Bottle &options, yarp::sig::Vector &target, const std::string& objName, const std::string handToUse)
{
    std::string hand="";
    for (int i=0; i<options.size(); i++)
    {
        yarp::os::Value val=options.get(i);
        if (val.isString())
        {
            std::string item=val.asString();
            if ((item=="left") || (item=="right"))
            {
                hand=item;
                break;
            }
        }
    }

    // always choose hand
    if (hand.empty())
    {
        if (handToUse.empty())
        {
            hand=(target[1]>0.0?"right":"left");
            options.addString(hand.c_str());
        }
        else
            hand=handToUse;
    }

    // apply 3D correction
    if (calibPort.getOutputCount()>0)
    {
        yarp::os::Bottle cmd,reply;
        cmd.addString("get_location");
        cmd.addString(hand);
        cmd.addString(objName);
        cmd.addString("iol-"+hand);
        calibPort.write(cmd,reply);
        target[0]=reply.get(1).asDouble();
        target[1]=reply.get(2).asDouble();
        target[2]=reply.get(3).asDouble();

        yarp::os::Bottle opt;
        opt.addString("fixate");
        look(target,opt,objName);
    }

    lastlyUsedHand=hand;
}

bool wysiwyd::wrdac::SubSystem_ARE::sendCmd(const yarp::os::Bottle &cmd, const bool disableATT)
{
    bool ret=false;

    std::string status;
    if (ATTconnected && disableATT)
    {
        SubATT->getStatus(status);
        if (status!="quiet")
            SubATT->stop();
    }

    yDebug() << "Send to ARE: " << cmd.toString();
    yarp::os::Bottle bReply;
    if (cmdPort.write(const_cast<yarp::os::Bottle&>(cmd),bReply))
        ret=(bReply.get(0).asVocab()==yarp::os::Vocab::encode("ack"));

    if (ATTconnected && disableATT)
    {
        if (status=="auto")
            SubATT->enableAutoMode();
        else if (status!="quiet")
            SubATT->track(status);
    }
    yDebug() << "Reply from ARE: " << bReply.toString();

    return ret;
}

bool wysiwyd::wrdac::SubSystem_ARE::connect()
{
    ABMconnected=SubABM->Connect();
    if (ABMconnected) {
        yInfo()<<"ARE connected to ABM";
    } else {
        yWarning()<<"ARE didn't connect to ABM";
    }

    ATTconnected=SubATT->Connect();
    if (ATTconnected) {
        yInfo()<<"ARE connected to Attention";
    } else {
        yDebug()<<"ARE didn't connect to Attention";
    }

    if (!yarp::os::Network::isConnected(calibPort.getName(),"/iolReachingCalibration/rpc")) {
        if (yarp::os::Network::connect(calibPort.getName(),"/iolReachingCalibration/rpc")) {
            yInfo()<<"ARE connected to calibrator";
        } else {
            yWarning()<<"ARE didn't connect to calibrator";
        }
    }

    bool ret=true;
    if(!yarp::os::Network::isConnected(cmdPort.getName(),"/actionsRenderingEngine/cmd:io")) {
        ret&=yarp::os::Network::connect(cmdPort.getName(),"/actionsRenderingEngine/cmd:io");
    }
    if(!yarp::os::Network::isConnected(rpcPort.getName(),"/actionsRenderingEngine/rpc")) {
        ret&=yarp::os::Network::connect(rpcPort.getName(),"/actionsRenderingEngine/rpc");
    }
    if(!yarp::os::Network::isConnected(getPort.getName(),"/actionsRenderingEngine/get:io")) {
        ret&=yarp::os::Network::connect(getPort.getName(),"/actionsRenderingEngine/get:io");
    }

    opc->connect("OPC");

    return ret;
}

wysiwyd::wrdac::SubSystem_ARE::SubSystem_ARE(const std::string &masterName) : SubSystem(masterName)
{
    SubABM = new SubSystem_ABM(m_masterName+"/from_ARE");
    SubATT = new SubSystem_Attention(m_masterName+"/from_ARE");

    cmdPort.open(("/" + masterName + "/" + SUBSYSTEM_ARE + "/cmd:io").c_str());
    rpcPort.open(("/" + masterName + "/" + SUBSYSTEM_ARE + "/rpc").c_str());
    getPort.open(("/" + masterName + "/" + SUBSYSTEM_ARE + "/get:io").c_str());
    calibPort.open(("/" + masterName + "/" + SUBSYSTEM_ARE + "/calib:io").c_str());
    m_type = SUBSYSTEM_ARE;
    lastlyUsedHand="";

    opc = new OPCClient(m_masterName+"/opc_from_ARE");
}

void wysiwyd::wrdac::SubSystem_ARE::Close()
{
    opc->interrupt();
    opc->close();
    delete opc;

    cmdPort.interrupt();
    rpcPort.interrupt();
    getPort.interrupt();
    calibPort.interrupt();

    SubABM->Close();
    SubATT->Close();

    cmdPort.close();
    rpcPort.close();
    getPort.close();
    calibPort.close();
}

bool wysiwyd::wrdac::SubSystem_ARE::getTableHeight(double &height)
{
    yarp::os::Bottle bCmd, bReply;
    bCmd.addVocab(yarp::os::Vocab::encode("get"));
    bCmd.addVocab(yarp::os::Vocab::encode("table"));
    getPort.write(bCmd, bReply);

    yarp::os::Value vHeight = bReply.find("table_height");
    if (vHeight.isNull()) {
        yError("No table height specified in ARE!");
        return false;
    }
    else {
        height = vHeight.asDouble();
        return true;
    }
}

yarp::sig::Vector wysiwyd::wrdac::SubSystem_ARE::applySafetyMargins(const yarp::sig::Vector &in)
{
    yarp::sig::Vector out=in;
    out[0]=std::min(out[0],-0.1);

    double height;
    if (getTableHeight(height))
        out[2]=std::max(out[2],height);

    return out;
}

bool wysiwyd::wrdac::SubSystem_ARE::home(const std::string &part)
{
    yDebug() << "ARE::home start";
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("home"));
    bCmd.addString(part.c_str());
    // send the result of recognition to the ABM
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(part, "argument"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "home", "action", lArgument, true);
    }
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(part, "argument"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action","home","action",lArgument,false);
    }
    yDebug() << "ARE::home stop";

    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::take(const std::string &sName, const yarp::os::Bottle &options)
{
    yDebug() << "ARE::take start";

    Entity* e = opc->getEntity(sName, true);
    Object *o;
    if(e) {
        o = dynamic_cast<Object*>(e);
    }
    else {
        yError() << sName << " is not an Entity";
        return false;
    }
    if(!o) {
        yError() << "Could not cast" << e->name() << "to Object";
        return false;
    }

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("take", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "take", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("take"));

    yarp::sig::Vector target=o->m_ego_position;
    yarp::os::Bottle opt=options;
    selectHandCorrectTarget(opt,target,sName);
    target=applySafetyMargins(target);
    appendCartesianTarget(bCmd,target);
    bCmd.append(opt);

    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("take", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        SubABM->sendActivity("action", "take", "action", lArgument, false);
    }
    yDebug() << "ARE::take stop";

    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::push(const std::string &sName, const yarp::os::Bottle &options)
{
    yDebug() << "ARE::push start";

    Entity* e = opc->getEntity(sName, true);
    Object *o;
    if(e) {
        o = dynamic_cast<Object*>(e);
    }
    else {
        yError() << sName << " is not an Entity";
        return false;
    }
    if(!o) {
        yError() << "Could not cast" << e->name() << "to Object";
        return false;
    }

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("push", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "push", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("push"));

    yarp::sig::Vector target=o->m_ego_position;
    yarp::os::Bottle opt=options;
    selectHandCorrectTarget(opt,target,sName);
    target=applySafetyMargins(target);
    appendCartesianTarget(bCmd,target);
    bCmd.append(opt);

    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("push", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "push", "action", lArgument, false);
    }
    yDebug() << "ARE::push stop";
    return bReturn;

}

bool wysiwyd::wrdac::SubSystem_ARE::point(const yarp::sig::Vector &targetUnsafe, const yarp::os::Bottle &options, const std::string &sName)
{
    yDebug() << "ARE::pointfar start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(targetUnsafe.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("pointfar", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "pointfar", "action", lArgument, true);
    }

    yarp::sig::Vector target=applySafetyMargins(targetUnsafe);

    yarp::os::Bottle bCmd;
    if(target[0]<-0.31) {
        bCmd.addVocab(yarp::os::Vocab::encode("pfar"));
    } else {
        bCmd.addVocab(yarp::os::Vocab::encode("point"));
    }

    appendCartesianTarget(bCmd,target);
    bCmd.append(options);

    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(targetUnsafe.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("pointfar", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "pointfar", "action", lArgument, false);
    }
    yDebug() << "ARE::pointfar stop";
    return bReturn;
}


/*bool wysiwyd::wrdac::SubSystem_ARE::point_old(const std::string &sName, const yarp::os::Bottle &options)
{
    yDebug() << "ARE::point start";

    Entity* e = opc->getEntity(sName, true);
    Object *o;
    if(e) {
        o = dynamic_cast<Object*>(e);
    }
    else {
        yError() << sName << " is not an Entity";
        return false;
    }
    if(!o) {
        yError() << "Could not cast" << e->name() << "to Object";
        return false;
    }

    if(o->m_ego_position[0]<-0.42) {
        yWarning() << "This object is too far for ARE::point. Use ARE::pointfar instead.";
        return pointfar(o->m_ego_position, options, sName);
    }

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("point", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "point", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("point"));

    yarp::sig::Vector target=o->m_ego_position;
    yarp::os::Bottle opt=options;
    selectHandCorrectTarget(opt,target,sName);
    target=applySafetyMargins(target);
    appendCartesianTarget(bCmd,target);
    bCmd.append(opt);

    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(o->m_ego_position.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("point", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "point", "action", lArgument, false);
    }
    yDebug() << "ARE::point stop";
    return bReturn;
}*/

bool wysiwyd::wrdac::SubSystem_ARE::drop(const yarp::os::Bottle &options)
{
    yDebug() << "ARE::drop start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        lArgument.push_back(std::pair<std::string, std::string>("drop", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        SubABM->sendActivity("action", "drop", "action", lArgument, true);
    }

    // we don't need d2k correction
    // because the drop takes place
    // on a random location
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("drop"));
    bCmd.append(options);
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("drop", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "drop", "action", lArgument, true);
    }
    yDebug() << "ARE::drop stop";
    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::dropOn(const yarp::sig::Vector &targetUnsafe, const yarp::os::Bottle &options)
{
    yDebug() << "ARE::dropOn start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(targetUnsafe.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "dropOn", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("drop"));
    bCmd.addString("over");

    yarp::sig::Vector target=targetUnsafe;
    yarp::os::Bottle opt=options;
    selectHandCorrectTarget(opt,target,lastlyUsedHand);
    target=applySafetyMargins(target);
    appendCartesianTarget(bCmd,target);
    bCmd.append(opt);

    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(targetUnsafe.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "dropOn", "action", lArgument, false);
    }
    yDebug() << "ARE::dropOn stop";
    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::observe(const yarp::os::Bottle &options)
{
    yDebug() << "ARE::observe start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "observe", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("observe"));
    bCmd.append(options);
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "observe", "action", lArgument, false);
    }
    yDebug() << "ARE::observe stop";

    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::expect(const yarp::os::Bottle &options)
{
    yDebug() << "ARE::expect start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "expect", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("expect"));
    bCmd.append(options);
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "expect", "action", lArgument, false);
    }
    yDebug() << "ARE::expect stop";

    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::give(const yarp::os::Bottle &options)
{
    yDebug() << "ARE::give start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "give", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("give"));
    bCmd.append(options);
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "give", "action", lArgument, false);
    }
    yDebug() << "ARE::give stop";
    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::waving(const bool sw)
{
    yDebug() << "ARE::waving start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "waving", "action", lArgument, true);
    }
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("waveing"));
    bCmd.addString(sw ? "on" : "off");
    bool bReturn = rpcPort.asPort().write(bCmd);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "waving", "action", lArgument, false);
    }
    yDebug() << "ARE::waving stop";
    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::look(const yarp::sig::Vector &target, const yarp::os::Bottle &options, const std::string &sName)
{
    yDebug() << "ARE::look start";
    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(target.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("look", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "look", "action", lArgument, true);
    }

    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("look"));
    appendCartesianTarget(bCmd, target);
    bCmd.append(options);
    bool bReturn = sendCmd(bCmd,true);
    std::string status;
    bReturn ? status = "success" : status = "failed";

    yarp::os::Time::delay(0.1);

    if (ABMconnected)
    {
        std::list<std::pair<std::string, std::string> > lArgument;
        lArgument.push_back(std::pair<std::string, std::string>(target.toString().c_str(), "vector"));
        lArgument.push_back(std::pair<std::string, std::string>(options.toString().c_str(), "options"));
        lArgument.push_back(std::pair<std::string, std::string>("look", "predicate"));
        lArgument.push_back(std::pair<std::string, std::string>(sName, "object"));
        lArgument.push_back(std::pair<std::string, std::string>("iCub", "agent"));
        lArgument.push_back(std::pair<std::string, std::string>(m_masterName, "provider"));
        lArgument.push_back(std::pair<std::string, std::string>(status, "status"));
        lArgument.push_back(std::pair<std::string, std::string>("ARE", "subsystem"));
        SubABM->sendActivity("action", "look", "action", lArgument, false);
    }
    yDebug() << "ARE::look stop";
    return bReturn;
}

bool wysiwyd::wrdac::SubSystem_ARE::track(const yarp::sig::Vector &target, const yarp::os::Bottle &options)
{
    // track() is meant for streaming => no point in gating the activity continuously
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("track"));
    appendCartesianTarget(bCmd, target);
    bCmd.append(options);
    return sendCmd(bCmd);
}

bool wysiwyd::wrdac::SubSystem_ARE::impedance(const bool sw)
{
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("impedance"));
    bCmd.addString(sw ? "on" : "off");
    return rpcPort.asPort().write(bCmd);
}

bool wysiwyd::wrdac::SubSystem_ARE::setExecTime(const double execTime)
{
    yarp::os::Bottle bCmd;
    bCmd.addVocab(yarp::os::Vocab::encode("time"));
    bCmd.addDouble(execTime);
    return rpcPort.asPort().write(bCmd);
}

wysiwyd::wrdac::SubSystem_ARE::~SubSystem_ARE()
{
    delete SubABM;
    delete SubATT;
}
