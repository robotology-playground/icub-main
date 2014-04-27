// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

// Copyright: (C) 2010 RobotCub Consortium
// Authors: Lorenzo Natale, Francesco Giovannini
// CopyPolicy: Released under the terms of the GNU GPL v2.0.

#include <CanBusSkin.h>

#include <yarp/os/Time.h>

#include <iostream>
#include <sstream>
#include <deque>


const int CAN_DRIVER_BUFFER_SIZE = 2047;


using namespace std;
using yarp::os::Bottle;
using yarp::os::Property;
using yarp::os::Value;
using yarp::dev::CanMessage;

bool CanBusSkin::open(yarp::os::Searchable& config) {
    bool correct=true;
#ifndef NODEBUG
    fprintf(stderr, "%s\n", config.toString().c_str());
#endif

    // Mandatory parameters for all skin patches
    correct &= config.check("canbusDevice");
    correct &= config.check("canDeviceNum");
    correct &= config.check("skinCanIds");
    correct &= config.check("period");

    if (!correct)
    {
        std::cerr<<"Error: insufficient parameters to CanBusSkin. \n"; 
        return false;
    }

    int period=config.find("period").asInt();
    setRate(period);

    Bottle ids=config.findGroup("skinCanIds").tail();

    if (ids.size()>1)
    {
        cerr << "WARNING: CANBUSSKIN: id list contains more than one entry -> devices will be merged. "<<endl;
    }
    for (int i=0; i<ids.size(); i++)
    {
        int id = ids.get(i).asInt();
        cardId.push_back (id);
        #ifndef NODEBUG
            fprintf(stderr, "Id reading from %d\n", id);
        #endif
    }
    
    /* ******* Parameters for hand skin patches. If these are not specified, default values are used. ******* */
    // Initialise parameter vectors
    int nCards = ids.size();
    // 4C Message
    msg4C_Timer = config.findGroup("4C_Timer").tail();                // 4C_Timer
    msg4C_CDCOffsetL = config.findGroup("4C_CDCOffsetL").tail();      // 4C_CDCOffsetL
    msg4C_CDCOffsetH = config.findGroup("4C_CDCOffsetH").tail();      // 4C_CDCOffsetR
    msg4C_TimeL = config.findGroup("4C_TimeL").tail();                // 4C_TimeL
    msg4C_TimeH = config.findGroup("4C_TimeH").tail();                // 4C_TimeH
    // 4E Message
    msg4E_Shift = config.findGroup("4E_Shift").tail();                // 4E_Shift
    msg4E_Shift3_1 = config.findGroup("4E_Shift3_1").tail();          // 4E_Shift3_1
    msg4E_NoLoad = config.findGroup("4E_NoLoad").tail();              // 4E_NoLoad
    msg4E_Param = config.findGroup("4E_Param").tail();                // 4E_Param
    msg4E_EnaL = config.findGroup("4E_EnaL").tail();                  // 4E_EnaL
    msg4E_EnaH = config.findGroup("4E_EnaH").tail();                  // 4E_EnaH
    
    // Check parameter list length
    // 4C Message
    checkParameterListLength("4C_Timer", msg4C_Timer, nCards, 0x01);
    checkParameterListLength("4C_CDCOffsetL", msg4C_CDCOffsetL, nCards, 0x00);
    checkParameterListLength("4C_CDCOffsetH", msg4C_CDCOffsetH, nCards, 0x20);
    checkParameterListLength("4C_TimeL", msg4C_TimeL, nCards, 0x00);
    checkParameterListLength("4C_TimeH", msg4C_TimeH, nCards, 0x00);
    // 4C Message
    checkParameterListLength("4E_Shift", msg4E_Shift, nCards, 0x02);
    checkParameterListLength("4E_Shift3_1", msg4E_Shift3_1, nCards, 0x22);
    checkParameterListLength("4E_NoLoad", msg4E_NoLoad, nCards, 0xF0);
    checkParameterListLength("4E_Param", msg4E_Param, nCards, 0x00);
    checkParameterListLength("4E_EnaL", msg4E_EnaL, nCards, 0xFF);
    checkParameterListLength("4E_EnaH", msg4E_EnaH, nCards, 0xFF);
    /* ********************************************** */
    
    Property prop;
    prop.put("device", config.find("canbusDevice").asString().c_str());
    prop.put("physDevice", config.find("physDevice").asString().c_str());
    prop.put("canTxTimeout", 500);
    prop.put("canRxTimeout", 500);
    prop.put("canDeviceNum", config.find("canDeviceNum").asInt());
    prop.put("canMyAddress", 0);
    prop.put("canTxQueueSize", CAN_DRIVER_BUFFER_SIZE);
    prop.put("canRxQueueSize", CAN_DRIVER_BUFFER_SIZE);

    pCanBus=0;
    pCanBufferFactory=0;

    driver.open(prop);
    if (!driver.isValid())
    {
        fprintf(stderr, "Error opening PolyDriver check parameters\n");
        return false;
    }

    driver.view(pCanBus);
    
    if (!pCanBus)
    {
        fprintf(stderr, "Error opening /ecan device not available\n");
        return false;
    }

    driver.view(pCanBufferFactory);
    pCanBus->canSetBaudRate(0); //default 1MB/s

    for (unsigned int i=0; i<cardId.size(); i++)
        for (unsigned int id=0; id<16; ++id)
        {
            pCanBus->canIdAdd(0x300+(cardId[i]<<4)+id);
        }

    outBuffer=pCanBufferFactory->createBuffer(CAN_DRIVER_BUFFER_SIZE);
    inBuffer=pCanBufferFactory->createBuffer(CAN_DRIVER_BUFFER_SIZE);

    //elements are:
    sensorsNum=16*12*cardId.size();
    data.resize(sensorsNum);
    data.zero();


    /* ****** Skin diagnostics ****** */
    useDiagnostics = false;     // FG: This flag could be set via configuration file for cleaningness sake.
    portSkinDiagnosticsOut.open("/diagnostics/skin/errors:o");

    RateThread::start();
    return true;
}

bool CanBusSkin::close() {
    RateThread::stop();
    if (pCanBufferFactory) 
    {
        pCanBufferFactory->destroyBuffer(inBuffer);
        pCanBufferFactory->destroyBuffer(outBuffer);
    }
    driver.close();
    return true;
}

int CanBusSkin::read(yarp::sig::Vector &out) {
    mutex.wait();
    out=data;
    diagnoseSkin();
    mutex.post();

    return yarp::dev::IAnalogSensor::AS_OK;
}

int CanBusSkin::getState(int ch)
{
    return yarp::dev::IAnalogSensor::AS_OK;;
}

int CanBusSkin::getChannels()
{
    return sensorsNum;
}

int CanBusSkin::calibrateSensor() {
    sendCANMessage4C();

    return AS_OK;
}

int CanBusSkin::calibrateChannel(int ch, double v)
{
    //NOT YET IMPLEMENTED
    return calibrateSensor();
}

int CanBusSkin::calibrateSensor(const yarp::sig::Vector& v)
{
    return 0;
}

int CanBusSkin::calibrateChannel(int ch)
{
    return 0;
}


bool CanBusSkin::threadInit() {
    if(sendCANMessage4C()) {
        return sendCANMessage4E();
    } else {
        return false;
    }
}

void CanBusSkin::run() {

    mutex.wait();

    unsigned int canMessages = 0;
    bool res = pCanBus->canRead(inBuffer, CAN_DRIVER_BUFFER_SIZE, &canMessages);
    
    if (!res) {
        std::cerr << "ERROR: CanBusSkin: CanRead failed \n";
    } else {
        for (unsigned int i = 0; i < canMessages; i++) {
            // Allocate error vector
            errors.resize(canMessages);

            CanMessage &msg = inBuffer[i];

            unsigned int msgid = msg.getId();
            unsigned int id = (msgid & 0x00F0) >> 4;
            unsigned int sensorId = (msgid & 0x000F);
            unsigned int msgType = (int) msg.getData()[0];
            int len = msg.getLen();

#if 0
            cout << "DEBUG: CanBusSkin: Board ID (" << id <<"): Sensor ID (" << sensorId << "): " 
                << "Message type (" << std::uppercase << std::showbase << std::hex << (int) msg.getData()[0] << ") " 
                << std::nouppercase << std::noshowbase << std::dec << " Length (" << len << "): ";
            cout << "Content: " << std::uppercase << std::showbase << std::hex;
            for (int k = 0; k < len; ++k) {
                cout << (int) msg.getData()[k] << " ";
            }
            cout << "\n" << std::nouppercase << std::noshowbase << std::dec;
#endif

            for (int j = 0; j < cardId.size(); j++) {
                if (id == cardId[j]) {
                    int index = 16*12*j + sensorId*12;
                    
                    if (msgType == 0x40) {
                        // Message head
                        for(int k = 0; k < 7; k++) {
                            data[index + k] = msg.getData()[k + 1];
                        }
                    } else if (msgType == 0xC0) {
                        // Message tail
                        for(int k = 0; k < 5; k++) {
                            data[index + k + 7] = msg.getData()[k + 1];
                        }

                        // Skin diagnostics
                        if (len == 8) {
                            // Skin diagnostics is active
                            useDiagnostics = true;

                            // Get error code head and tail
                            short head = msg.getData()[6];
                            short tail = msg.getData()[7];
                            int fullMsg = (head << 8) | (tail & 0xFF);

                            // Store error message
                            errors[i].board = id;
                            errors[i].sensor = sensorId;
                            errors[i].error = fullMsg;
                        } else {
#if 0
                            cout << "WARNING: CanBusSkin: Board ID (" << id << "): You are using the old skin firmware which does not include skin diagnostics. You might want to consider upgrading to a newer firmware. \n";
#endif
                        }
                    }
                }
            }
        }
    }


    mutex.post();

}

void CanBusSkin::threadRelease() {
#ifndef NODEBUG
    printf("DEBUG: CanBusSkin Thread releasing...\n");    
    printf("DEBUG: ... done.\n");
#endif
}


/* *********************************************************************************************************************** */
/* ******* Diagnose skin errors.                                            ********************************************** */
bool CanBusSkin::diagnoseSkin(void) {
    using iCub::skin::diagnostics::DetectedError;   // FG: Skin diagnostics errors
    using yarp::sig::Vector;

    if (useDiagnostics) {
        // Write errors to port
        for (size_t i = 0; i < errors.size(); ++i) {
            Vector &out = portSkinDiagnosticsOut.prepare();
            out.clear();
            
            out.push_back(errors[i].board);
            out.push_back(errors[i].sensor);
            out.push_back(errors[i].error);
            
            portSkinDiagnosticsOut.write(true);
        }
    }

    return true;
}
/* *********************************************************************************************************************** */


/* *********************************************************************************************************************** */
/* ******* Converts input parameter bottle into a std vector.               ********************************************** */
void CanBusSkin::checkParameterListLength(const string &i_paramName, Bottle &i_paramList, const int &i_length, const Value &i_defaultValue) {
    if ((i_paramList.isNull()) || (i_paramList.size() != i_length)) {
        cout << "WARNING: CanBusSkin: The number of " << i_paramName << " parameters provided is different than the number of CAN IDs for this BUS. Check your parameters. \n";
        cout << "WARNING: CanBusSkin: Using default values for " << i_paramName << " parameters. \n";

        size_t i = i_paramList.size();
        for (i ; i <= i_length; ++i) {
            i_paramList.add(i_defaultValue);
        }
    }
}
/* *********************************************************************************************************************** */


/* *********************************************************************************************************************** */
/* ******* Sends CAN setup message type 4C.                                 ********************************************** */
bool CanBusSkin::sendCANMessage4C(void) {
    for (size_t i = 0; i < cardId.size(); ++i) {
#ifndef NODEBUG
        printf("DEBUG: CanBusSkin: Thread initialising board ID: %d. \n",cardId[i]);
        printf("DEBUG: CanBusSkin: Sending 0x4C message to skin boards. \n");
#endif 

        unsigned int canMessages=0;
        unsigned id = 0x200 + cardId[i];
       
        CanMessage &msg4c=outBuffer[0];
        msg4c.setId(id);
        msg4c.getData()[0] = 0x4C; // Message type
        msg4c.getData()[1] = 0x01; 
        msg4c.getData()[2] = 0x01; 
        msg4c.getData()[3] = msg4C_Timer.get(i).asInt();
        msg4c.getData()[4] = msg4C_CDCOffsetL.get(i).asInt();
        msg4c.getData()[5] = msg4C_CDCOffsetH.get(i).asInt();
        msg4c.getData()[6] = msg4C_TimeL.get(i).asInt();
        msg4c.getData()[7] = msg4C_TimeH.get(i).asInt();
        msg4c.setLen(8);

#ifndef NODEBUG
        cout << "DEBUG: CanBusSkin: Input parameters (msg 4C) are: " << std::hex << std::showbase
            << (int) msg4c.getData()[0] << " " << (int) msg4c.getData()[1] << " " << (int) msg4c.getData()[2] << " "
            << msg4C_Timer.get(i).asInt() << " " << msg4C_CDCOffsetL.get(i).asInt() << " " << msg4C_CDCOffsetH.get(i).asInt() << " " 
            << msg4C_TimeL.get(i).asInt() << " " << msg4C_TimeH.get(i).asInt() << std::dec << std::noshowbase
            << ". \n";
        cout << "DEBUG: CanBusSkin: Output parameters (msg 4C) are: " << std::hex << std::showbase
            << (int) msg4c.getData()[0] << " " << (int) msg4c.getData()[1] << " " << (int) msg4c.getData()[2] << " " 
            << (int) msg4c.getData()[3] << " " << (int) msg4c.getData()[4] << " " << (int) msg4c.getData()[5] << " " 
            << (int) msg4c.getData()[6] << " " << (int) msg4c.getData()[7] << std::dec << std::noshowbase
            << ". \n";
#endif

        if (!pCanBus->canWrite(outBuffer, 1, &canMessages)) {
            cerr << "ERROR: CanBusSkin: Could not write to the CAN interface. \n";
            return false;
        }
    }

    return true;
}
/* *********************************************************************************************************************** */


/* *********************************************************************************************************************** */
/* ******* Sends CAN setup message type 4E.                                 ********************************************** */
bool CanBusSkin::sendCANMessage4E(void) {
    // Send 0x4E message to modify offset
    for (size_t i = 0; i < cardId.size(); ++i) {
        // FG: Sending 4E message to all MTBs
#ifndef NODEBUG
        printf("CanBusSkin: Thread initialising board ID: %d. \n", cardId[i]);
        printf("CanBusSkin: Sending 0x4E message to skin boards. \n");
#endif 
    
        unsigned int canMessages = 0;
        unsigned id = 0x200 + cardId[i];
                
        CanMessage &msg4e = outBuffer[0];
        msg4e.setId(id);
        msg4e.getData()[0] = 0x4E; // Message type
        msg4e.getData()[1] = msg4E_Shift.get(i).asInt(); 
        msg4e.getData()[2] = msg4E_Shift3_1.get(i).asInt(); 
        msg4e.getData()[3] = msg4E_NoLoad.get(i).asInt();
        msg4e.getData()[4] = msg4E_Param.get(i).asInt();
        msg4e.getData()[5] = msg4E_EnaL.get(i).asInt();
        msg4e.getData()[6] = msg4E_EnaH.get(i).asInt();
        msg4e.getData()[7] = 0x0A;
        msg4e.setLen(8);
            
#ifndef NODEBUG
        cout << "DEBUG: CanBusSkin: Input parameters (msg 4E) are: " << std::hex << std::showbase
            << (int) msg4e.getData()[0] << " " << msg4E_Shift.get(i).asInt() << " " << msg4E_Shift3_1.get(i).asInt() << " " 
            << msg4E_NoLoad.get(i).asInt() << " " << msg4E_Param.get(i).asInt() << " " << msg4E_EnaL.get(i).asInt() << " " 
            << msg4E_EnaH.get(i).asInt() << " " << (int) msg4e.getData()[7] << std::dec << std::noshowbase
            << ". \n";
        cout << "DEBUG: CanBusSkin: Output parameters (msg 4E) are: " << std::hex << std::showbase
            << (int) msg4e.getData()[0] << " " << (int) msg4e.getData()[1] << " " << (int) msg4e.getData()[2] << " " 
            << (int) msg4e.getData()[3] << " " << (int) msg4e.getData()[4] << " " << (int) msg4e.getData()[5] << " " 
            << (int) msg4e.getData()[6] << " " << (int) msg4e.getData()[7] << std::dec << std::noshowbase
            << ". \n";
#endif

        if (!pCanBus->canWrite(outBuffer, 1, &canMessages)) {
            cerr << "ERROR: CanBusSkin: Could not write to the CAN interface. \n";
            return false;
        }
    }

    return true;
}
/* *********************************************************************************************************************** */
