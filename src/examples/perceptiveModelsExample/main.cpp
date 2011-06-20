/* 
 * Copyright (C) 2011 RobotCub Consortium, European Commission FP6 Project IST-004370
 * Author: Ugo Pattacini
 * email:  ugo.pattacini@iit.it
 * website: www.robotcub.org
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

/** 
\defgroup perceptiveModelsExample perceptiveModelsExample
 
@ingroup icub_module  
 
Example module for the use of \ref PerceptiveModels library.

Copyright (C) 2011 RobotCub Consortium
 
Author: Ugo Pattacini 

CopyPolicy: Released under the terms of the GNU GPL v2.0. 

\section lib_sec Libraries 
- YARP libraries. 
- \ref PerceptiveModels library.  

\section tested_os_sec Tested OS
Windows, Linux

\author Ugo Pattacini
*/ 

#include <string>
#include <stdio.h>

#include <yarp/os/Network.h>
#include <yarp/os/Value.h>
#include <yarp/os/Property.h>
#include <yarp/os/RFModule.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/ControlBoardInterfaces.h>

#include <iCub/perception/springyFingers.h>

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;
using namespace iCub::perception;


class ExampleModule: public RFModule
{
	SpringyFingersModel model;
    PolyDriver driver;
    bool calibrate;

    IPositionControl *ipos;
    IEncoders        *ienc;

    double min,max,*val;
    int joint;

public:
    ExampleModule() : calibrate(true) { }

    bool configure(ResourceFinder &rf)
    {
        string name=rf.find("name").asString().c_str();
        string type=rf.find("type").asString().c_str();

        Property driverOpt("(device remote_controlboard)");
        driverOpt.put("remote",("/icub/"+type+"_arm").c_str());
        driverOpt.put("local",("/"+name).c_str());
        if (!driver.open(driverOpt))
            return false;

        driver.view(ipos);
        driver.view(ienc);

        IControlLimits *ilim;
        driver.view(ilim);

        joint=12;   // index joint
        ilim->getLimits(joint,&min,&max);
        double margin=0.1*(max-min);
        min=min+margin;
        max=max-margin;
        val=&min;

        Property genOpt;
        genOpt.put("name",(name+"/springy").c_str());
        genOpt.put("type",type.c_str());
        genOpt.put("verbose",1);
        string general(genOpt.toString().c_str());
        string thumb( "(thumb  (name thumb))");
        string index( "(index  (name index))");
        string middle("(middle (name middle))");
        string ring(  "(ring   (name ring))");
        string little("(little (name little))");

		Property options((general+" "+thumb+" "+index+" "+middle+" "+ring+" "+little).c_str());
		fprintf(stdout,"configuring options: %s\n",options.toString().c_str());

        return model.fromProperty(options);
    }

    bool close()
    {
        driver.close();

		Property options;
		model.toProperty(options);
		fprintf(stdout,"saving options: %s\n",options.toString().c_str());
        
        return true;
    }

    double getPeriod()
	{
		return 0.25;
	}

    bool updateModule()
	{
        if (calibrate)
        {
            Property options("(finger index)");
            model.calibrate(options);
            calibrate=false;

            ipos->setRefAcceleration(joint,1e9);
            ipos->setRefSpeed(joint,30.0);
            ipos->positionMove(joint,*val);            
        }
        else
        {
            Node *finger=model.getNode("index");
            Value data; finger->getSensorsData(data);
            Value out; model.getOutput(out);
            fprintf(stdout,"%s sensors data = %s\nmodel output = %s\n\n",
                    finger->getName().c_str(),data.toString().c_str(),out.toString().c_str());
            
            double fb;
            ienc->getEncoder(joint,&fb);
            if (fabs(*val-fb)<5.0)
            {
                if (val==&min)
                    val=&max;
                else
                    val=&min;

                ipos->positionMove(joint,*val);
            }
        }

		return true;
	}
};


int main(int argc, char *argv[])
{
    Network yarp;	

    if (!yarp.checkNetwork())
        return -1;

    ResourceFinder rf;
    rf.setVerbose(true);
    rf.setDefaultContext("perceptiveModelsExample/conf");
    rf.setDefaultConfigFile("config.ini");
    rf.setDefault("name","percex");
    rf.setDefault("type","right");
    rf.configure("ICUB_ROOT",argc,argv);

    ExampleModule mod;

    return mod.runModule(rf);
}




